#import "MLNCompassButton_Private.h"
#import "MLNCompassDirectionFormatter.h"

#import "MLNGeometry.h"

#import "MLNMapView_Private.h"
#import "UIImage+MLNAdditions.h"
#import "NSBundle+MLNAdditions.h"

#include <mbgl/math/wrap.hpp>

@interface MLNCompassButton ()

@property (nonatomic, weak) MLNMapView *mapView;
@property (nonatomic) MLNCompassDirectionFormatter *accessibilityCompassFormatter;

@end

@implementation MLNCompassButton

+ (instancetype)compassButtonWithMapView:(MLNMapView *)mapView {
    return [[MLNCompassButton alloc] initWithMapView:mapView];
}

- (instancetype)initWithMapView:(MLNMapView *)mapView {
    if (self = [super init]) {
        self.mapView = mapView;
        [self commonInit];
    }
    return self;
}

- (void)commonInit {
    self.image = self.compassImage;

    self.compassVisibility = MLNOrnamentVisibilityAdaptive;

    self.alpha = 0;
    self.userInteractionEnabled = YES;
    self.translatesAutoresizingMaskIntoConstraints = NO;

    UITapGestureRecognizer *tapGesture = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTapGesture:)];
    [self addGestureRecognizer:tapGesture];

    self.accessibilityTraits = UIAccessibilityTraitButton;
    self.accessibilityLabel = NSLocalizedStringWithDefaultValue(@"COMPASS_A11Y_LABEL", nil, nil, @"Compass", @"Accessibility label");
    self.accessibilityHint = NSLocalizedStringWithDefaultValue(@"COMPASS_A11Y_HINT", nil, nil, @"Rotates the map to face due north", @"Accessibility hint");

    self.accessibilityCompassFormatter = [[MLNCompassDirectionFormatter alloc] init];
    self.accessibilityCompassFormatter.unitStyle = NSFormattingUnitStyleLong;

    [self sizeToFit];
}

- (void)setCompassVisibility:(MLNOrnamentVisibility)compassVisibility {
    if (_compassVisibility == compassVisibility) { return; }
    _compassVisibility = compassVisibility;

    [self updateCompassAnimated:NO];
}

- (UIImage *)compassImage {
    UIImage *scaleImage = [UIImage mgl_resourceImageNamed:@"Compass"];
    UIGraphicsBeginImageContextWithOptions(scaleImage.size, NO, UIScreen.mainScreen.scale);
    [scaleImage drawInRect:{CGPointZero, scaleImage.size}];

    UIFont *northFont;
    if (@available(iOS 13.0, *)) {
        northFont = [UIFont systemFontOfSize:11 weight:UIFontWeightLight];
    } else {
        northFont = [UIFont systemFontOfSize:11 weight:UIFontWeightUltraLight];
    }

    NSAttributedString *north = [[NSAttributedString alloc] initWithString:NSLocalizedStringWithDefaultValue(@"COMPASS_NORTH", nil, nil, @"N", @"Compass abbreviation for north") attributes:@{
        NSFontAttributeName: northFont,
        NSForegroundColorAttributeName: [UIColor whiteColor],
    }];
    CGRect stringRect = CGRectMake((scaleImage.size.width - north.size.width) / 2,
                                   scaleImage.size.height * 0.435,
                                   north.size.width, north.size.height);
    [north drawInRect:stringRect];

    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

- (void)handleTapGesture:(__unused UITapGestureRecognizer *)sender {
    [self.mapView resetNorth];
}

- (void)updateCompass {
    [self updateCompassAnimated:YES];
}

- (void)updateCompassAnimated:(BOOL)animated {
    CLLocationDirection direction = self.mapView.direction;
    CLLocationDirection plateDirection = mbgl::util::wrap(-direction, 0., 360.);
    self.transform = CGAffineTransformMakeRotation(MLNRadiansFromDegrees(plateDirection));

    self.isAccessibilityElement = direction > 0;
    self.accessibilityValue = [self.accessibilityCompassFormatter stringFromDirection:direction];

    switch (self.compassVisibility) {
        case MLNOrnamentVisibilityAdaptive:
            if (direction > 0 && self.alpha < 1) {
                [self showCompass:animated];
            } else if (direction == 0 && self.alpha > 0) {
                [self hideCompass:animated];
            }
            break;
        case MLNOrnamentVisibilityVisible:
            [self showCompass:animated];
            break;
        case MLNOrnamentVisibilityHidden:
            [self hideCompass:animated];
            break;
    }
}

- (void)showCompass:(BOOL)animated {
    animated ? [self animateToAlpha:1] : [self setAlpha:1];
}

- (void)hideCompass:(BOOL)animated {
    animated ? [self animateToAlpha:0] : [self setAlpha:0];
    self.isAccessibilityElement = NO;
    self.accessibilityElementsHidden = YES;
}

- (void)animateToAlpha:(CGFloat)alpha {
    [UIView animateWithDuration:MLNAnimationDuration delay:0 options:UIViewAnimationOptionBeginFromCurrentState animations:^{
         self.alpha = alpha;
     } completion:nil];
}

@end
