#import <Foundation/Foundation.h>
#include "yozora_accent.h"
#include "yozora_address.h"
#include "yozora_core.h"

typedef enum {
    YZLanguageRussian,
    YZLanguageEnglish,
    YZLanguageJapanese
} YZLanguage;

typedef enum {
    YZThemeLight,
    YZThemeDark,
    YZThemeBlack
} YZThemeKind;

/* posted after any setting changed, the object is the YZSettings class */
extern NSString *const YZSettingsDidChangeNotification;

/* what the user chose in the settings, kept in the user defaults */
@interface YZSettings : NSObject
+ (YZLanguage)language;
+ (void)setLanguage:(YZLanguage)language;
+ (YZThemeKind)theme;
+ (void)setTheme:(YZThemeKind)theme;
+ (yozora_search_engine)searchEngine;
+ (void)setSearchEngine:(yozora_search_engine)core;
+ (BOOL)blockTrackers;
+ (void)setBlockTrackers:(BOOL)enabled;
/* tiles of the page are drawn at one pixel for each point, which is faster and softer */
+ (BOOL)economyRaster;
+ (void)setEconomyRaster:(BOOL)enabled;
+ (BOOL)saveHistory;
+ (void)setSaveHistory:(BOOL)enabled;
+ (BOOL)javascript;
+ (void)setJavascript:(BOOL)enabled;
+ (BOOL)blockThirdPartyCookies;
+ (void)setBlockThirdPartyCookies:(BOOL)enabled;
/* pages are asked for fewer animations and run them at 30 frames a second, on by default for phones under a gigabyte */
+ (BOOL)reduceMotion;
+ (void)setReduceMotion:(BOOL)enabled;
/* fonts that sites send are loaded and drawn */
+ (BOOL)webFonts;
+ (void)setWebFonts:(BOOL)enabled;
/* the size of page text in percent */
+ (NSInteger)textZoom;
+ (void)setTextZoom:(NSInteger)percent;
/* the range of the slider, in percent */
#define YZTextZoomMin 50
#define YZTextZoomMax 200
/* the accent chosen by the person, NO while the palette purple is kept. the private session keeps its own
   purple either way */
+ (BOOL)customAccent:(yozora_accent *)accent;
+ (void)setCustomAccent:(yozora_accent)accent;
+ (void)resetAccent;
/* the accent of the private session, NO while its deep purple is kept, the private palette turns to it as the
   normal one does */
+ (BOOL)customPrivateAccent:(yozora_accent *)accent;
+ (void)setCustomPrivateAccent:(yozora_accent)accent;
+ (void)resetPrivateAccent;
/* developer settings: the corner overlay, the log capture and the jit (on by default, read once at start, so
   a change counts at the next launch) */
+ (BOOL)debugOverlay;
+ (void)setDebugOverlay:(BOOL)enabled;
+ (BOOL)logging;
+ (void)setLogging:(BOOL)enabled;
/* freed objects are kept and named when a message reaches one: read at launch, a change counts at the next one */
+ (BOOL)zombies;
+ (void)setZombies:(BOOL)enabled;
+ (BOOL)jit;
+ (void)setJit:(BOOL)enabled;
/* the person has answered the question about the jit, or set the switch */
+ (BOOL)jitAnswered;
/* main calls this before it reads the jit: the mark of a launch with the jit that did not finish turns the jit
   off. endJitStart clears the mark once the browser is up. the flag says the jit was turned off that way */
+ (void)beginJitStart;
+ (void)endJitStart;
+ (BOOL)jitDisabledAfterFailedStart;
/* not stored, the app starts in the normal profile. purples the interface, keeps pages out of the history and
   gives the core its private session. call it before the first private page is created and after the last
   profile page is parked */
+ (BOOL)incognito;
+ (void)setIncognito:(BOOL)enabled;
/* page translation: the target language (a code of core/yozora_translate.h, the device language until
   picked), the languages never offered and the ones translated at once */
+ (NSString *)translateTarget;
+ (void)setTranslateTarget:(NSString *)code;
+ (BOOL)translateNeverOffers:(NSString *)code;
+ (void)setTranslateNeverOffers:(BOOL)never language:(NSString *)code;
+ (BOOL)translatesAtOnce:(NSString *)code;
+ (void)setTranslatesAtOnce:(BOOL)always language:(NSString *)code;
/* the user's rules for the blocker in adblock plus syntax (core/yozora_filter.h), kept in
   Library/Yozora/adblock-rules.txt. setting them gives them to the core and fills the report. NO with an
   error text when the file cannot be written */
+ (NSString *)adblockRules;
+ (BOOL)setAdblockRules:(NSString *)text report:(yozora_adblock_report *)report error:(NSString **)error;
/* hands every setting that the page core knows to it, call once at start */
+ (void)applyToCore;
@end
