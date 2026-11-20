#import <UIKit/UIKit.h>
#include "yozora_core.h"
#import "YZTranslation.h"

/* a tab owns its suspended core until the controller takes it back */
@interface YZTab : NSObject {
    NSString *_identifier;
    NSString *_url;
    NSString *_title;
    UIImage *_thumbnail;
    BOOL _thumbnailOnDisk;
    BOOL _isPrivate;
    int _scrollY;
    BOOL _desktop;
    yozora_core *_suspendedCore;
    YZTranslation *_translation;
}
/* names the stored preview, kept with the tab in the saved list */
@property(nonatomic, copy) NSString *identifier;
@property(nonatomic, copy) NSString *url;
@property(nonatomic, copy) NSString *title;
/* the preview, also written to the caches directory unless the tab is private and read back when the memory
   copy was dropped. nil means no preview */
@property(nonatomic, retain) UIImage *thumbnail;
/* a private tab keeps its preview in memory only */
@property(nonatomic, assign) BOOL isPrivate;
/* restored once the reloaded page has finished */
@property(nonatomic, assign) int scrollY;
/* asked for as a desktop browser would, kept while the core is destroyed and made again, forgotten by a
   restart */
@property(nonatomic, assign) BOOL desktop;
/* what is being done to translate the page of the tab, with the core it belongs to: it lives as long as that core does */
@property(nonatomic, retain) YZTranslation *translation;
- (BOOL)isBlank;
/* the memory copy of the preview goes, the stored one stays and is read again when the tray asks for it */
- (void)dropThumbnailFromMemory;
/* a preview of an earlier run is waiting in the caches directory */
- (void)noteStoredThumbnail;
/* the stored preview is deleted, for a tab that was closed */
- (void)removeStoredThumbnail;
/* deletes the stored previews whose names are not in the list, for the ones left by tabs that are gone */
+ (void)removeStoredThumbnailsExcept:(NSArray *)identifiers;
- (void)keepSuspendedCore:(yozora_core *)core;
- (yozora_core *)takeSuspendedCore;
- (void)discardSuspendedCore;
@end
