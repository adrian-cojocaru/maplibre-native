#pragma once

#include <mbgl/actor/mailbox.hpp>
#include <mbgl/actor/scheduler.hpp>

#include <memory>
#include <utility>

#include <jni/jni.hpp>

namespace mbgl {
namespace android {

/**
 * The MapRendererRunnable is a peer class that encapsulates
 * a scheduled mailbox in a Java Runnable so it can be
 * scheduled on the map renderer thread.
 *
 */
class MapRendererRunnable {
public:
    static constexpr auto Name() { return "org/maplibre/android/maps/renderer/MapRendererRunnable"; };

    static void registerNative(jni::JNIEnv&);

    MapRendererRunnable(jni::JNIEnv&, Scheduler::Task&&);

    // Only for jni registration, unused
    MapRendererRunnable(jni::JNIEnv&) { assert(false); }

    ~MapRendererRunnable();

    void run(jni::JNIEnv&);

    // Transfers ownership of the Peer object to the caller
    jni::Global<jni::Object<MapRendererRunnable>> peer();

private:
    jni::Global<jni::Object<MapRendererRunnable>> javaPeer;
    Scheduler::Task function;
};

} // namespace android
} // namespace mbgl
