package org.maplibre.android.integration

import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import androidx.test.filters.LargeTest
import androidx.test.internal.runner.junit4.AndroidJUnit4ClassRunner
import org.junit.Assert
import org.junit.Assume
import org.junit.Test
import org.junit.runner.RunWith
import org.maplibre.android.maps.MapLibreMap
import org.maplibre.android.maps.MapView
import org.maplibre.android.testapp.activity.EspressoTest
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Tests for how a SurfaceView backed MapView hands surface changes to its render thread. See
 * [#4705](https://github.com/maplibre/maplibre-native/pull/4705).
 *
 * Nothing here blocks the instrumentation thread on the UI thread without a deadline, so a UI
 * thread stuck on the render thread fails a test instead of hanging the run.
 */
@RunWith(AndroidJUnit4ClassRunner::class)
class SurfaceViewRenderThreadTest : EspressoTest() {

    private val uiHandler = Handler(Looper.getMainLooper())

    /**
     * `surfaceChanged` used to wait for the render thread to pick up the new size. Anything that
     * keeps the render thread busy (backend initialization, shader compilation, a stalled swapchain
     * acquire) then froze the UI thread for as long, and eventually caused an ANR.
     *
     * The render thread is parked on a latch, the MapView is resized, and the UI thread has to come
     * back from `surfaceChanged` while the render thread is still parked.
     */
    @Test
    @LargeTest
    fun resizeDoesNotWaitForBusyRenderThread() {
        validateTestSetup()
        val surfaceView = surfaceView()
        // Only the Vulkan SurfaceView resizes without waiting for the render thread so far.
        Assume.assumeTrue(
            "Not the Vulkan SurfaceView",
            surfaceView.javaClass.simpleName == "MapLibreVulkanSurfaceView"
        )
        assertMapRendersAFrame("before resizing")

        // Callbacks run in registration order, so the probe sees surfaceChanged only after
        // MapLibreSurfaceView#surfaceChanged has returned.
        val probe = SurfaceChangedProbe()
        onUiThreadWithin("adding the surface probe") { surfaceView.holder.addCallback(probe) }

        val renderThreadParked = CountDownLatch(1)
        val releaseRenderThread = CountDownLatch(1)
        mapView.queueEvent {
            renderThreadParked.countDown()
            releaseRenderThread.await(RENDER_THREAD_STALL_MS, TimeUnit.MILLISECONDS)
        }

        try {
            Assert.assertTrue(
                "The render thread did not pick up the parking event",
                renderThreadParked.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)
            )

            var targetHeight = 0
            val resizeRequested = SystemClock.elapsedRealtime()
            onUiThreadWithin("requesting a resize") {
                targetHeight = mapView.height / 2
                val params = mapView.layoutParams
                params.height = targetHeight
                mapView.layoutParams = params
            }

            if (!probe.changed.await(UI_THREAD_BUDGET_MS, TimeUnit.MILLISECONDS)) {
                val uiStack = Looper.getMainLooper().thread.stackTrace
                    .take(UI_STACK_DEPTH)
                    .joinToString("\n    at ")
                Assert.fail(
                    "surfaceChanged did not return within $UI_THREAD_BUDGET_MS ms while the render thread " +
                        "was busy. The UI thread is waiting for the render thread:\n    at $uiStack"
                )
            }
            val elapsed = SystemClock.elapsedRealtime() - resizeRequested
            Assert.assertEquals("surfaceChanged reported an unexpected height", targetHeight, probe.height)
            Assert.assertTrue("The resize took $elapsed ms on the UI thread", elapsed < UI_THREAD_BUDGET_MS)
            onUiThreadWithin("a no-op after the resize", UI_THREAD_BUDGET_MS) {}
        } finally {
            releaseRenderThread.countDown()
            uiHandler.post { surfaceView.holder.removeCallback(probe) }
        }

        // The new size must still reach the renderer once the render thread is free again.
        assertMapRendersAFrame("after resizing")
    }

    /**
     * Hiding the SurfaceView destroys its surface while the renderer stays alive. A snapshot
     * requested in that state is delivered by the first frame after the surface comes back. The
     * Vulkan backend used to drop the read-back request together with the surface and then
     * dereference the missing read-back image in that frame, killing the process.
     */
    @Test
    @LargeTest
    fun snapshotRequestedWhileHidden() {
        validateTestSetup()
        val surfaceView = surfaceView()
        assertMapRendersAFrame("before hiding")

        onUiThreadWithin("hiding the SurfaceView") { surfaceView.visibility = View.GONE }

        val snapshot = AtomicReference<Bitmap?>()
        val delivered = CountDownLatch(1)
        onUiThreadWithin("requesting a snapshot while hidden") {
            maplibreMap.snapshot(MapLibreMap.SnapshotReadyCallback { bitmap ->
                snapshot.set(bitmap)
                delivered.countDown()
            })
        }

        // Render thread events run in order, so once this one has run the snapshot request has
        // been handled without a surface.
        val requestHandled = CountDownLatch(1)
        mapView.queueEvent { requestHandled.countDown() }
        Assert.assertTrue(
            "The render thread did not handle the snapshot request",
            requestHandled.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)
        )

        onUiThreadWithin("showing the SurfaceView") { surfaceView.visibility = View.VISIBLE }

        Assert.assertTrue(
            "The snapshot was not delivered after the surface came back",
            delivered.await(FRAME_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        )
        Assert.assertNotNull("The snapshot is empty", snapshot.get())
        assertMapRendersAFrame("after the snapshot")
    }

    private fun surfaceView(): SurfaceView {
        val surfaceView = mapView.renderView as? SurfaceView
        Assume.assumeTrue("MapView is not backed by a SurfaceView", surfaceView != null)
        return surfaceView!!
    }

    /** Runs [action] on the UI thread and fails unless it finishes within [timeoutMs]. */
    private fun onUiThreadWithin(what: String, timeoutMs: Long = TIMEOUT_MS, action: () -> Unit) {
        val done = CountDownLatch(1)
        val failure = AtomicReference<Throwable?>()
        uiHandler.post {
            try {
                action()
            } catch (throwable: Throwable) {
                failure.set(throwable)
            } finally {
                done.countDown()
            }
        }
        Assert.assertTrue(
            "$what did not run within $timeoutMs ms, the UI thread is blocked",
            done.await(timeoutMs, TimeUnit.MILLISECONDS)
        )
        failure.get()?.let { throw AssertionError("$what failed", it) }
    }

    private fun assertMapRendersAFrame(context: String) {
        val rendered = CountDownLatch(1)
        val listener = MapView.OnDidFinishRenderingFrameListener { _, _, _ -> rendered.countDown() }
        onUiThreadWithin("requesting a repaint ($context)") {
            mapView.addOnDidFinishRenderingFrameListener(listener)
            maplibreMap.triggerRepaint()
        }
        try {
            Assert.assertTrue(
                "The map did not render a frame within $FRAME_TIMEOUT_MS ms ($context)",
                rendered.await(FRAME_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            )
        } finally {
            uiHandler.post { mapView.removeOnDidFinishRenderingFrameListener(listener) }
        }
    }

    private class SurfaceChangedProbe : SurfaceHolder.Callback {
        val changed = CountDownLatch(1)

        @Volatile
        var height = 0

        override fun surfaceCreated(holder: SurfaceHolder) {}

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            this.height = height
            changed.countDown()
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {}
    }

    companion object {
        /** Budget for UI thread work that is expected to be immediate. */
        const val TIMEOUT_MS: Long = 5000L

        /** Budget for the map to produce a frame once it has been asked to repaint. */
        const val FRAME_TIMEOUT_MS: Long = 15000L

        /** How long the render thread stays parked unless the test releases it earlier. */
        const val RENDER_THREAD_STALL_MS: Long = 10000L

        /** How long a resize may keep the UI thread busy. A few frames, with plenty of slack. */
        const val UI_THREAD_BUDGET_MS: Long = 1000L

        /** UI thread frames to report when the resize blocks. */
        const val UI_STACK_DEPTH = 12
    }
}
