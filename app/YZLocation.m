#import "YZLocation.h"
#import <CoreLocation/CoreLocation.h>
#import <UIKit/UIKit.h>
#include <math.h>

@interface YZLocation () <CLLocationManagerDelegate> {
    CLLocationManager *_manager;
    yozora_core *_core;
    BOOL _running;
    BOOL _highAccuracy;
}
@end

@implementation YZLocation

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    // a manager running behind a hidden app holds the radio and gps for nothing, the app has no location
    // background mode. it stops here and starts again at the next frame when a page still watches
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationHidden) name:UIApplicationDidEnterBackgroundNotification object:nil];
    return self;
}

- (void)applicationHidden {
    if (!_running)
        return;
    [_manager stopUpdatingLocation];
    _running = NO;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_manager stopUpdatingLocation];
    _manager.delegate = nil;
    [_manager release];
    [super dealloc];
}

- (void)updateForCore:(yozora_core *)core wanted:(BOOL)wanted highAccuracy:(BOOL)highAccuracy {
    if (!wanted || !core) {
        if (_running) {
            [_manager stopUpdatingLocation];
            _running = NO;
        }
        return;
    }
    if (_running && _core == core && _highAccuracy == highAccuracy)
        return;
    if (![CLLocationManager locationServicesEnabled] || [CLLocationManager authorizationStatus] == kCLAuthorizationStatusDenied
        || [CLLocationManager authorizationStatus] == kCLAuthorizationStatusRestricted) {
        // the person turned the location of the system off for the app: the page hears a denial and the manager is not started
        if (_core != core || !_running) {
            _core = core;
            yozora_core_geolocation_error(core, 1, "Location services are off for Yozora");
        }
        return;
    }
    if (!_manager) {
        _manager = [[CLLocationManager alloc] init];
        _manager.delegate = self;
    }
    // ios 8 and later give no position until the app asked for it, a manager started without the question
    // stays silent
    if ([CLLocationManager authorizationStatus] == kCLAuthorizationStatusNotDetermined && [_manager respondsToSelector:NSSelectorFromString(@"requestWhenInUseAuthorization")])
        [_manager performSelector:NSSelectorFromString(@"requestWhenInUseAuthorization")];
    _manager.desiredAccuracy = highAccuracy ? kCLLocationAccuracyBest : kCLLocationAccuracyHundredMeters;
    // a watch that moves less than this is not worth a wake of the processor
    _manager.distanceFilter = highAccuracy ? kCLDistanceFilterNone : 50.0;
    _core = core;
    _highAccuracy = highAccuracy;
    [_manager startUpdatingLocation];
    _running = YES;
}

- (void)detachCore:(yozora_core *)core {
    if (_core != core)
        return;
    [_manager stopUpdatingLocation];
    _running = NO;
    _core = NULL;
}

// ios 2 to 6 only know this callback, later systems still call it when didUpdateLocations: is not implemented
- (void)locationManager:(CLLocationManager *)manager didUpdateToLocation:(CLLocation *)location fromLocation:(CLLocation *)oldLocation {
    (void)manager;
    (void)oldLocation;
    if (!_core || location.horizontalAccuracy < 0)
        return;
    // the system hands over its last known fix first and it can be hours old, a page that asked now must not
    // be told it
    if (fabs([location.timestamp timeIntervalSinceNow]) > 30)
        return;
    double nothing = NAN;
    yozora_core_geolocation_position(_core, location.coordinate.latitude, location.coordinate.longitude, location.horizontalAccuracy,
        location.verticalAccuracy >= 0 ? location.altitude : nothing, location.verticalAccuracy >= 0 ? location.verticalAccuracy : nothing,
        location.course >= 0 ? location.course : nothing, location.speed >= 0 ? location.speed : nothing,
        [location.timestamp timeIntervalSince1970] * 1000.0);
}

// the person answers the system question (ios 8 and later) or takes the permission back while a page watches
- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    (void)manager;
    if (!_core || (status != kCLAuthorizationStatusDenied && status != kCLAuthorizationStatusRestricted))
        return;
    yozora_core_geolocation_error(_core, 1, "Location services are off for Yozora");
    [_manager stopUpdatingLocation];
    _running = NO;
}

- (void)locationManager:(CLLocationManager *)manager didFailWithError:(NSError *)error {
    (void)manager;
    if (!_core)
        return;
    BOOL denied = [[error domain] isEqualToString:kCLErrorDomain] && [error code] == kCLErrorDenied;
    yozora_core_geolocation_error(_core, denied ? 1 : 0, [[error localizedDescription] UTF8String]);
    // a denial ends the updates, a lost signal does not
    if (denied)
        _running = NO;
}

@end
