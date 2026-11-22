#import "YZSitesController.h"
#import "YZClassic.h"
#import "YZCompat.h"
#import "YZCrash.h"
#import "YZDialog.h"
#import "YZStrings.h"
#import "YZTheme.h"
#include "yozora_core.h"

static const CGFloat YZSitesBarHeight = 64;
static const CGFloat YZSitesPillHeight = 32;
static const CGFloat YZSitesRowHeight = 52;
// the longest address a row holds, the core keeps keys of at most 512 bytes
static const size_t YZSitesKeyCapacity = 600;
static NSString *const YZSitesRowIdentifier = @"yozora.sites.row";

@implementation YZSitesController

- (void)dealloc {
    [YZCrash noteLifecycle:@"dealloc" object:self];
    _table.delegate = nil;
    _table.dataSource = nil;
    [_dialog dismissSilently];
    [_dialog release];
    [_card release];
    [_bar release];
    [_titleLabel release];
    [_subtitleLabel release];
    [_clear release];
    [_kinds release];
    [_table release];
    [_keys release];
    [_allowed release];
    [_fill removeFromSuperview];
    [_fill release];
    [super dealloc];
}

- (yozora_site_kind)shownKind {
    switch (_kinds.selectedSegment) {
    case 1: return YOZORA_SITE_NOTIFICATIONS;
    case 2: return YOZORA_SITE_ADBLOCK_PAUSED;
    default: return YOZORA_SITE_GEOLOCATION;
    }
}

- (void)loadView {
    UIView *root = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]];
    self.view = root;
    [root release];
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = [YZTheme sheetRadius];
    _card.clipsToBounds = YES;
    [self.view addSubview:_card];
    _bar = [[UIView alloc] initWithFrame:CGRectZero];
    [_card addSubview:_bar];
    _titleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _titleLabel.backgroundColor = [UIColor clearColor];
    _titleLabel.font = [YZTheme boldSystemFontOfSize:17];
    _titleLabel.text = YZT(@"Разрешения сайтов");
    [_bar addSubview:_titleLabel];
    _subtitleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _subtitleLabel.backgroundColor = [UIColor clearColor];
    _subtitleLabel.font = [YZTheme systemFontOfSize:12];
    [_bar addSubview:_subtitleLabel];
    _clear = [[UIButton buttonWithType:UIButtonTypeCustom] retain];
    _clear.frame = CGRectMake(0, 0, 44, 44);
    _clear.layer.cornerRadius = 22;
    _clear.accessibilityLabel = YZT(@"Сбросить список");
    [_clear addTarget:self action:@selector(clearPressed) forControlEvents:UIControlEventTouchUpInside];
    [_bar addSubview:_clear];
    _kinds = [[YZSegmentedPill alloc] initWithTitles:[NSArray arrayWithObjects:YZT(@"Место"), YZT(@"Уведомления"), YZT(@"Реклама"), nil] icons:nil];
    [_kinds addTarget:self action:@selector(kindChanged) forControlEvents:UIControlEventValueChanged];
    [_card addSubview:_kinds];
    _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _table.backgroundColor = [UIColor clearColor];
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.allowsSelection = NO;
    _table.dataSource = self;
    _table.delegate = self;
    [_card addSubview:_table];
    _keys = [[NSMutableArray alloc] init];
    _allowed = [[NSMutableArray alloc] init];
}

- (void)restyle {
    self.view.backgroundColor = [YZTheme screenColor];
    YZPaintBehindScreens(self.navigationController);
    [YZTheme styleTransparentNavigationBar:self.navigationController.navigationBar];
    if (!_fill)
        _fill = [[YZBarFill alloc] initWithFrame:CGRectZero];
    [_fill applyTheme];
    YZInstallBarTitle(self, YZT(@"Разрешения сайтов"), self, @selector(back));
    _card.backgroundColor = [YZTheme cardColor];
    _titleLabel.textColor = [YZTheme textColor];
    _subtitleLabel.textColor = [YZTheme subtleTextColor];
    [YZClassic styleGlyphKey:_clear icon:YZIconDelete size:22 modernRadius:22];
    [_kinds applyTheme];
    [self.view setNeedsLayout];
}

- (void)back { [self.navigationController popViewControllerAnimated:YES]; }

- (void)reload {
    [_keys removeAllObjects];
    [_allowed removeAllObjects];
    yozora_site_kind kind = [self shownKind];
    char key[YZSitesKeyCapacity];
    int allowed = 0;
    for (size_t i = 0; yozora_site_entry(kind, i, key, sizeof key, &allowed); ++i) {
        NSString *name = [NSString stringWithUTF8String:key];
        if (!name)
            continue;
        [_keys addObject:name];
        [_allowed addObject:[NSNumber numberWithBool:allowed != 0]];
    }
    [_table reloadData];
    _clear.enabled = [_keys count] > 0;
    _clear.alpha = [_keys count] ? 1 : 0.4f;
    _subtitleLabel.text = [_keys count] ? [NSString stringWithFormat:@"%lu", (unsigned long)[_keys count]] : YZT(@"Пока никаких решений");
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [YZCrash note:@"screen YZSitesController"];
    [self restyle];
    [self reload];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = self.view.bounds.size.width, height = self.view.bounds.size.height;
    CGFloat top = [YZTheme barOverlap:self.navigationController.navigationBar];
    YZInstallBarTitle(self, YZT(@"Разрешения сайтов"), self, @selector(back));
    [_fill placeInView:self.view controller:self];
    CGFloat side = 12;
    _card.frame = CGRectMake(side, top + 4, width - 2 * side, MAX((CGFloat)0, height - top - 4 - 12));
    CGFloat cw = _card.bounds.size.width;
    _bar.frame = CGRectMake(0, 0, cw, YZSitesBarHeight);
    _clear.center = CGPointMake(cw - 16 - 22, YZSitesBarHeight / 2);
    CGFloat textWidth = MAX((CGFloat)0, CGRectGetMinX(_clear.frame) - 8 - 20);
    _titleLabel.frame = CGRectMake(20, YZSitesBarHeight / 2 - 20, textWidth, 22);
    _subtitleLabel.frame = CGRectMake(20, YZSitesBarHeight / 2 + 2, textWidth, 16);
    _kinds.frame = CGRectMake(12, YZSitesBarHeight, cw - 24, YZSitesPillHeight);
    CGFloat tableTop = YZSitesBarHeight + YZSitesPillHeight + 8;
    _table.frame = CGRectMake(0, tableTop, cw, MAX((CGFloat)0, _card.bounds.size.height - tableTop - 6));
}

- (void)kindChanged { [self reload]; }

- (void)clearPressed {
    if (_dialog || ![_keys count])
        return;
    __block YZSitesController *me = self;
    yozora_site_kind kind = [self shownKind];
    _dialog = [[YZDialog dialogWithTitle:YZT(@"Сбросить список?") message:YZT(@"Сайты спросят снова, а блокировщик заработает на всех.") cancelTitle:YZT(@"Отмена")
                            confirmTitle:YZT(@"Сбросить") destructive:YES handler:^(NSInteger index, NSArray *texts) {
        (void)texts;
        [me->_dialog release];
        me->_dialog = nil;
        if (index != 0) {
            yozora_site_clear(kind);
            [me reload];
        }
    }] retain];
    [_dialog showInView:self.view];
}

#pragma mark rows

- (NSInteger)tableView:(UITableView *)table numberOfRowsInSection:(NSInteger)section { return (NSInteger)[_keys count]; }

- (CGFloat)tableView:(UITableView *)table heightForRowAtIndexPath:(NSIndexPath *)path { return YZSitesRowHeight; }

- (UITableViewCell *)tableView:(UITableView *)table cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [table dequeueReusableCellWithIdentifier:YZSitesRowIdentifier];
    if (!cell) {
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:YZSitesRowIdentifier] autorelease];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        cell.backgroundColor = [UIColor clearColor];
        cell.textLabel.backgroundColor = [UIColor clearColor];
        cell.detailTextLabel.backgroundColor = [UIColor clearColor];
        cell.textLabel.font = [YZTheme systemFontOfSize:15];
        cell.detailTextLabel.font = [YZTheme systemFontOfSize:12];
    }
    NSUInteger index = (NSUInteger)path.row;
    if (index < [_keys count]) {
        BOOL allowed = [[_allowed objectAtIndex:index] boolValue];
        cell.textLabel.text = [_keys objectAtIndex:index];
        cell.textLabel.textColor = [YZTheme textColor];
        cell.detailTextLabel.textColor = [YZTheme subtleTextColor];
        if ([self shownKind] == YOZORA_SITE_ADBLOCK_PAUSED)
            cell.detailTextLabel.text = YZT(@"Блокировщик выключен");
        else
            cell.detailTextLabel.text = allowed ? YZT(@"Разрешено") : YZT(@"Запрещено");
    }
    return cell;
}

- (BOOL)tableView:(UITableView *)table canEditRowAtIndexPath:(NSIndexPath *)path { return YES; }

- (UITableViewCellEditingStyle)tableView:(UITableView *)table editingStyleForRowAtIndexPath:(NSIndexPath *)path { return UITableViewCellEditingStyleDelete; }

- (void)tableView:(UITableView *)table commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)path {
    if (style != UITableViewCellEditingStyleDelete || (NSUInteger)path.row >= [_keys count])
        return;
    yozora_site_remove([self shownKind], [[_keys objectAtIndex:(NSUInteger)path.row] UTF8String]);
    [self reload];
}

@end
