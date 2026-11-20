#import <UIKit/UIKit.h>
#include "yozora_core.h"

@interface YZSceneView : UIView {
    yozora_scene *_scene;
    NSMutableDictionary *_nodes;
}
- (void)showScene:(yozora_scene *)scene;
- (void)scrollWithCore:(yozora_core *)core;
- (void)clearScene;
/* the layer that carries the tiles of the scene layer with this id, nil when there is none. a picture put
   into it lies over that layer's tiles and under the layers above */
- (CALayer *)contentsLayerForLayerID:(uint64_t)identifier;
@end
