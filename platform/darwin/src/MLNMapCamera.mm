#import "MLNMapCamera.h"
#import "MLNGeometry_Private.h"
#import "MLNLoggingConfiguration_Private.h"

#import <CoreLocation/CoreLocation.h>

#include <mbgl/math/wrap.hpp>

BOOL MLNEqualFloatWithAccuracy(CGFloat left, CGFloat right, CGFloat accuracy)
{
    return MAX(left, right) - MIN(left, right) <= accuracy;
}

@implementation MLNMapCamera

+ (BOOL)supportsSecureCoding
{
    return YES;
}

+ (instancetype)camera
{
    return [[self alloc] init];
}

+ (instancetype)cameraLookingAtCenterCoordinate:(CLLocationCoordinate2D)centerCoordinate
                              fromEyeCoordinate:(CLLocationCoordinate2D)eyeCoordinate
                                    eyeAltitude:(CLLocationDistance)eyeAltitude
{
    CLLocationDirection heading = -1;
    CGFloat pitch = -1;
    if (CLLocationCoordinate2DIsValid(centerCoordinate) && CLLocationCoordinate2DIsValid(eyeCoordinate)) {
        heading = MLNDirectionBetweenCoordinates(eyeCoordinate, centerCoordinate);

        CLLocation *centerLocation = [[CLLocation alloc] initWithLatitude:centerCoordinate.latitude
                                                                longitude:centerCoordinate.longitude];
        CLLocation *eyeLocation = [[CLLocation alloc] initWithLatitude:eyeCoordinate.latitude
                                                             longitude:eyeCoordinate.longitude];
        CLLocationDistance groundDistance = [eyeLocation distanceFromLocation:centerLocation];
        CGFloat radianPitch = atan2(eyeAltitude, groundDistance);
        pitch = mbgl::util::wrap(90 - MLNDegreesFromRadians(radianPitch), 0.0, 360.0);
    }

    return [[self alloc] initWithCenterCoordinate:centerCoordinate
                                         altitude:eyeAltitude
                                            pitch:pitch
                                          heading:heading];
}

+ (instancetype)cameraLookingAtCenterCoordinate:(CLLocationCoordinate2D)centerCoordinate
                                 acrossDistance:(CLLocationDistance)distance
                                          pitch:(CGFloat)pitch
                                        heading:(CLLocationDirection)heading
{
    // Angle at the viewpoint formed by the straight lines running perpendicular
    // to the ground and toward the center coordinate.
    CLLocationDirection eyeAngle = 90 - pitch;
    CLLocationDistance altitude = distance * sin(MLNRadiansFromDegrees(eyeAngle));

    return [[self alloc] initWithCenterCoordinate:centerCoordinate
                                         altitude:altitude
                                            pitch:pitch
                                          heading:heading];
}

+ (instancetype)cameraLookingAtCenterCoordinate:(CLLocationCoordinate2D)centerCoordinate
                                       altitude:(CLLocationDistance)altitude
                                          pitch:(CGFloat)pitch
                                        heading:(CLLocationDirection)heading
{
    return [[self alloc] initWithCenterCoordinate:centerCoordinate
                                         altitude:altitude
                                            pitch:pitch
                                          heading:heading];
}

+ (instancetype)cameraLookingAtCenterCoordinate:(CLLocationCoordinate2D)centerCoordinate
                                   fromDistance:(CLLocationDistance)distance
                                          pitch:(CGFloat)pitch
                                        heading:(CLLocationDirection)heading
{
    // TODO: Remove this compatibility shim in favor of `-cameraLookingAtCenterCoordinate:acrossDistance:pitch:heading:.
    // https://github.com/mapbox/mapbox-gl-native/issues/12943
    return [MLNMapCamera cameraLookingAtCenterCoordinate:centerCoordinate
                                                altitude:distance
                                                   pitch:pitch
                                                 heading:heading];
}

- (instancetype)initWithCenterCoordinate:(CLLocationCoordinate2D)centerCoordinate
                                altitude:(CLLocationDistance)altitude
                                   pitch:(CGFloat)pitch
                                 heading:(CLLocationDirection)heading
{
    MLNLogDebug(@"Initializing withCenterCoordinate: %@ altitude: %.0fm pitch: %f° heading: %f°", MLNStringFromCLLocationCoordinate2D(centerCoordinate), altitude, pitch, heading);
    if (self = [super init])
    {
        _centerCoordinate = centerCoordinate;
        _altitude = altitude;
        _pitch = pitch;
        _heading = heading;
    }
    return self;
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
    MLNLogInfo(@"Initialiazing with coder.");
    if (self = [super init])
    {
        _centerCoordinate = CLLocationCoordinate2DMake([coder decodeDoubleForKey:@"centerLatitude"],
                                                       [coder decodeDoubleForKey:@"centerLongitude"]);
        _altitude = [coder decodeDoubleForKey:@"altitude"];
        _pitch = [coder decodeDoubleForKey:@"pitch"];
        _heading = [coder decodeDoubleForKey:@"heading"];
    }
    return self;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
    [coder encodeDouble:_centerCoordinate.latitude forKey:@"centerLatitude"];
    [coder encodeDouble:_centerCoordinate.longitude forKey:@"centerLongitude"];
    [coder encodeDouble:_altitude forKey:@"altitude"];
    [coder encodeDouble:_pitch forKey:@"pitch"];
    [coder encodeDouble:_heading forKey:@"heading"];
}

- (id)copyWithZone:(nullable NSZone *)zone
{
    return [[[self class] allocWithZone:zone] initWithCenterCoordinate:_centerCoordinate
                                                              altitude:_altitude
                                                                 pitch:_pitch
                                                               heading:_heading];
}

+ (NSSet<NSString *> *)keyPathsForValuesAffectingViewingDistance {
    return [NSSet setWithObjects:@"altitude", @"pitch", nil];
}

- (CLLocationDistance)viewingDistance {
    CLLocationDirection eyeAngle = 90 - self.pitch;
    return self.altitude / sin(MLNRadiansFromDegrees(eyeAngle));
}

- (void)setViewingDistance:(CLLocationDistance)distance {
    MLNLogDebug(@"Setting viewingDistance: %f", distance);
    CLLocationDirection eyeAngle = 90 - self.pitch;
    self.altitude = distance * sin(MLNRadiansFromDegrees(eyeAngle));
}

- (NSString *)description
{
    return [NSString stringWithFormat:@"<%@: %p; centerCoordinate = %f, %f; altitude = %.0fm; heading = %.0f°; pitch = %.0f°>",
            NSStringFromClass([self class]), (void *)self, _centerCoordinate.latitude, _centerCoordinate.longitude, _altitude, _heading, _pitch];
}

- (BOOL)isEqual:(id)other
{
    if ( ! [other isKindOfClass:[self class]])
    {
        return NO;
    }
    if (other == self)
    {
        return YES;
    }

    MLNMapCamera *otherCamera = other;
    return (_centerCoordinate.latitude == otherCamera.centerCoordinate.latitude
            && _centerCoordinate.longitude == otherCamera.centerCoordinate.longitude
            && _altitude == otherCamera.altitude
            && _pitch == otherCamera.pitch && _heading == otherCamera.heading);
}

- (BOOL)isEqualToMapCamera:(MLNMapCamera *)otherCamera
{
    if (otherCamera == self)
    {
        return YES;
    }

    return (MLNEqualFloatWithAccuracy(_centerCoordinate.latitude, otherCamera.centerCoordinate.latitude, 1e-8)
            && MLNEqualFloatWithAccuracy(_centerCoordinate.longitude, otherCamera.centerCoordinate.longitude, 1e-8)
            && MLNEqualFloatWithAccuracy(_altitude, otherCamera.altitude, 1e-6)
            && MLNEqualFloatWithAccuracy(_pitch, otherCamera.pitch, 1e-2)
            && MLNEqualFloatWithAccuracy(_heading, otherCamera.heading, 1e-2));
}

- (NSUInteger)hash
{
    return (@(_centerCoordinate.latitude).hash + @(_centerCoordinate.longitude).hash
            + @(_altitude).hash + @(_pitch).hash + @(_heading).hash);
}

@end
