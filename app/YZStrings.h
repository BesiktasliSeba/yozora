#import <Foundation/Foundation.h>

/* the interface text in the chosen language, with the Russian source text as a fallback */
NSString *YZT(NSString *russian);

/* a text the core made, with or without detail after a colon ("Не удалось начать переход: …"): the part before the colon is translated */
NSString *YZTCore(NSString *text);
