#import "YZLogController.h"
#import "YZClassic.h"
#import "YZCrash.h"
#import "YZCompat.h"
#import "YZControls.h"
#import "YZLog.h"
#import "YZMotion.h"
#import "YZSettings.h"
#import "YZStrings.h"
#import "YZTheme.h"

static const NSUInteger YZLogShownLines = 600;
// a row shows at most three lines of its text: a line of a page console can be 8 KiB
static const NSUInteger YZLogRowLines = 3;
static const CGFloat YZLogRowSide = 8;
static NSString *const YZLogRowIdentifier = @"yozora.log.row";
static const NSInteger YZLogRowLabelTag = 91;
static const CGFloat YZLogBarHeight = 64;
// the two rows of filters under the bar
static const CGFloat YZLogPillHeight = 32;
static const CGFloat YZLogFilterHeight = 6 + 32 + 6 + 32 + 6;

static UIFont *YZLogFont(CGFloat size) {
    UIFont *font = [UIFont fontWithName:@"JetBrainsMono-Regular" size:size];
    return font ? font : [UIFont fontWithName:@"Courier" size:size];
}

@implementation YZLogController

- (id)init {
    self = [super initWithNibName:nil bundle:nil];
    if (!self)
        return nil;
    _heading = [YZT(@"Логи") copy];
    _pinned = YES;
    _source = YZLogSourceCurrent;
    // the lines of level info and above: the debug lines of the core are many and are asked for with "all"
    _level = 1;
    return self;
}

- (void)dealloc {
    [YZCrash noteLifecycle:@"dealloc" object:self];
    [_timer invalidate];
    [_card release];
    [_bar release];
    [_badge release];
    [_badgeIcon release];
    [_titleLabel release];
    [_subtitleLabel release];
    [_copy release];
    [_clear release];
    _table.delegate = nil;
    _table.dataSource = nil;
    [_table release];
    [_rows release];
    [_rowLevels release];
    [_rowHeights release];
    [_sources release];
    [_levels release];
    [_fill removeFromSuperview];
    [_fill release];
    [_heading release];
    [super dealloc];
}

// the terminal takes the card color of the theme, the text the text color: a log is as light or dark as the rest of the interface
- (UIColor *)terminalColor { return [YZTheme cardColor]; }

- (UIButton *)roundButton:(YZIcon)icon action:(SEL)action label:(NSString *)label {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.frame = CGRectMake(0, 0, 44, 44);
    button.tag = icon;
    [YZClassic styleGlyphKey:button icon:icon size:22 modernRadius:22];
    button.accessibilityLabel = label;
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    YZMotionAttachBounce(button, 0.06f);
    [_bar addSubview:button];
    return button;
}

- (void)loadView {
    [YZCrash note:@"logs: loadView"];
    UIView *root = [[UIView alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]];
    self.view = root;
    [root release];
    _card = [[UIView alloc] initWithFrame:CGRectZero];
    _card.layer.cornerRadius = [YZTheme sheetRadius];
    _card.clipsToBounds = YES;
    [self.view addSubview:_card];
    _bar = [[UIView alloc] initWithFrame:CGRectZero];
    [_card addSubview:_bar];
    _badge = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 44, 44)];
    _badge.layer.cornerRadius = 22;
    [_bar addSubview:_badge];
    _badgeIcon = [[UIImageView alloc] initWithFrame:CGRectMake(10, 10, 24, 24)];
    _badgeIcon.image = [YZTheme icon:YZIconTerminal size:24 color:[YZTheme accentColor]];
    [_badge addSubview:_badgeIcon];
    _titleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _titleLabel.backgroundColor = [UIColor clearColor];
    _titleLabel.font = YZLogFont(14);
    _titleLabel.text = @"yozora ~ log";
    [_bar addSubview:_titleLabel];
    _subtitleLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _subtitleLabel.backgroundColor = [UIColor clearColor];
    _subtitleLabel.font = YZLogFont(10.5f);
    [_bar addSubview:_subtitleLabel];
    _copy = [[self roundButton:YZIconCopy action:@selector(copyLog) label:YZT(@"Скопировать журнал")] retain];
    _clear = [[self roundButton:YZIconDelete action:@selector(clearLog) label:YZT(@"Очистить журнал")] retain];
    _sources = [[YZSegmentedPill alloc] initWithTitles:[NSArray arrayWithObjects:YZT(@"Сейчас"), YZT(@"Прошлый"), YZT(@"Падения"), nil] icons:nil];
    _sources.selectedSegment = _source;
    [_sources addTarget:self action:@selector(filterChanged:) forControlEvents:UIControlEventValueChanged];
    [_card addSubview:_sources];
    _levels = [[YZSegmentedPill alloc] initWithTitles:[NSArray arrayWithObjects:YZT(@"Все"), YZT(@"Инфо"), YZT(@"Важные"), YZT(@"Ошибки"), nil] icons:nil];
    _levels.selectedSegment = _level;
    [_levels addTarget:self action:@selector(filterChanged:) forControlEvents:UIControlEventValueChanged];
    [_card addSubview:_levels];
    _table = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStylePlain];
    _table.backgroundColor = [UIColor clearColor];
    _table.separatorStyle = UITableViewCellSeparatorStyleNone;
    _table.allowsSelection = NO;
    _table.dataSource = self;
    _table.delegate = self;
    [_card addSubview:_table];
    [self restyle];
    [YZCrash note:@"logs: view built"];
}

- (void)restyle {
    self.view.backgroundColor = [YZTheme surfaceColor];
    YZPaintBehindScreens(self.navigationController);
    [YZTheme styleTransparentNavigationBar:self.navigationController.navigationBar];
    if (!_fill)
        _fill = [[YZBarFill alloc] initWithFrame:CGRectZero];
    [_fill applyTheme];
    YZInstallBarTitle(self, _heading, self, @selector(back));
    _card.backgroundColor = [self terminalColor];
    _badge.backgroundColor = [YZTheme stateColor];
    _badgeIcon.image = [YZTheme icon:YZIconTerminal size:24 color:[YZTheme accentColor]];
    _titleLabel.textColor = [YZTheme textColor];
    _subtitleLabel.textColor = [YZTheme subtleTextColor];
    for (UIButton *button in [NSArray arrayWithObjects:_copy, _clear, nil]) {
        [YZClassic styleGlyphKey:button icon:(YZIcon)button.tag size:22 modernRadius:22];
    }
    [_table reloadData];
    [_sources applyTheme];
    [_levels applyTheme];
    [self.view setNeedsLayout];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = self.view.bounds.size.width, height = self.view.bounds.size.height;
    CGFloat top = [YZTheme barOverlap:self.navigationController.navigationBar];
    YZInstallBarTitle(self, _heading, self, @selector(back));
    [_fill placeInView:self.view controller:self];
    CGFloat side = 12;
    _card.frame = CGRectMake(side, top + 4, width - 2 * side, MAX((CGFloat)0, height - top - 4 - 12));
    CGFloat cw = _card.bounds.size.width;
    _bar.frame = CGRectMake(0, 0, cw, YZLogBarHeight);
    _badge.center = CGPointMake(16 + 22, YZLogBarHeight / 2);
    _clear.center = CGPointMake(cw - 16 - 22, YZLogBarHeight / 2);
    _copy.center = CGPointMake(cw - 16 - 44 - 8 - 22, YZLogBarHeight / 2);
    CGFloat textLeft = 16 + 44 + 12, textWidth = MAX((CGFloat)0, CGRectGetMinX(_copy.frame) - 8 - textLeft);
    _titleLabel.frame = CGRectMake(textLeft, YZLogBarHeight / 2 - 19, textWidth, 20);
    _subtitleLabel.frame = CGRectMake(textLeft, YZLogBarHeight / 2 + 1, textWidth, 16);
    _sources.frame = CGRectMake(12, YZLogBarHeight + 6, cw - 24, YZLogPillHeight);
    _levels.frame = CGRectMake(12, YZLogBarHeight + 6 + YZLogPillHeight + 6, cw - 24, YZLogPillHeight);
    CGFloat textTop = YZLogBarHeight + YZLogFilterHeight;
    CGRect tableFrame = CGRectMake(0, textTop, cw, MAX((CGFloat)0, _card.bounds.size.height - textTop - 6));
    if (!CGRectEqualToRect(_table.frame, tableFrame)) {
        // the height of a row depends on the width of the table
        BOOL widthChanged = fabs(_table.frame.size.width - tableFrame.size.width) >= 1;
        _table.frame = tableFrame;
        if (widthChanged) {
            ++_generation;
            _shownRevision = (NSUInteger)-1;
            [self reload];
        }
    }
}

- (void)back { [self.navigationController popViewControllerAnimated:YES]; }

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [YZCrash note:[NSString stringWithFormat:@"screen %@", NSStringFromClass([self class])]];
    YZInstallBarTitle(self, _heading, self, @selector(back));
    [self restyle];
    _shownRevision = (NSUInteger)-1;
    _timer = [NSTimer timerWithTimeInterval:0.5 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

// rows are made once the screen has its size, a table of no width would ask the height of each of hundreds of
// rows for a width of nothing
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [YZCrash note:@"logs: appeared"];
    [self reload];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_timer invalidate];
    _timer = nil;
}

- (void)tick {
    // only the lines of this run grow: the run before and the crash record are read when they are chosen
    if (_source == YZLogSourceCurrent && [YZLog revision] != _shownRevision)
        [self reload];
}

- (void)filterChanged:(YZSegmentedPill *)pill {
    if (pill == _sources)
        _source = _sources.selectedSegment;
    else
        _level = _levels.selectedSegment;
    ++_generation;
    _shownRevision = (NSUInteger)-1;
    _pinned = YES;
    [self reload];
}

// a row shows up to three lines and the font has fixed width, so the line count is length over characters per line
static CGFloat YZLogRowHeight(NSString *text, NSUInteger perLine, CGFloat lineHeight) {
    NSUInteger lines = MIN((NSUInteger)YZLogRowLines, MAX((NSUInteger)1, ([text length] + perLine - 1) / perLine));
    return lines * lineHeight + 3;
}

// the card shows the newest lines, one row each, and follows the end while the person has not scrolled away from it.
// the lines are read, filtered, split, leveled and measured in the background: at a fast log the screen asks every
// half second and the main thread cannot spend that time on up to six thousand lines and six hundred rows
- (void)reload {
    if (_loading)
        return;
    _loading = YES;
    NSUInteger revision = [YZLog revision];
    NSUInteger generation = _generation;
    YZLogSource source = (YZLogSource)_source;
    int level = (int)_level;
    BOOL capturing = [YZLog capturing];
    // the font is measured here, once, and not for every row
    UIFont *font = YZLogFont(11);
    CGFloat character = MAX((CGFloat)1, YZTextSize(@"M", font).width);
    CGFloat lineHeight = ceil(font.lineHeight);
    CGFloat available = MAX(character, _table.bounds.size.width - 2 * YZLogRowSide);
    NSUInteger perLine = MAX((NSUInteger)1, (NSUInteger)floor(available / character));
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableArray *rows = [NSMutableArray array];
        NSMutableArray *levels = [NSMutableArray array];
        NSMutableArray *heights = [NSMutableArray array];
        @autoreleasepool {
            NSString *tail = [YZLog textForSource:source minimumLevel:level lines:YZLogShownLines];
            if ([tail length]) {
                for (NSString *line in [tail componentsSeparatedByString:@"\n"]) {
                    if (![line length])
                        continue;
                    [rows addObject:line];
                    [levels addObject:[NSNumber numberWithInt:source == YZLogSourceCrashes ? 1 : [YZLog levelOfLine:line]]];
                }
            }
            if (![rows count]) {
                NSString *message;
                if (source == YZLogSourcePrevious)
                    message = @"# no log of the run before: logging was off, or this is the first run with it on";
                else if (source == YZLogSourceCrashes)
                    message = @"# no crashes recorded";
                else if (!capturing)
                    message = @"# logging is off, turn it on in Settings > Developer";
                else
                    message = level > 0 ? @"# no lines of this level yet" : @"# waiting for the first line";
                [rows addObject:message];
                [levels addObject:[NSNumber numberWithInt:0]];
            }
            for (NSString *row in rows)
                [heights addObject:[NSNumber numberWithDouble:YZLogRowHeight(row, perLine, lineHeight)]];
        }
        NSString *state = source == YZLogSourcePrevious ? @"previous run" : source == YZLogSourceCrashes ? @"crashes" : (capturing ? @"live" : @"off");
        dispatch_async(dispatch_get_main_queue(), ^{
            [self showRows:rows levels:levels heights:heights state:state revision:revision generation:generation];
        });
    });
}

- (void)showRows:(NSArray *)rows levels:(NSArray *)levels heights:(NSArray *)heights state:(NSString *)state
        revision:(NSUInteger)revision generation:(NSUInteger)generation {
    _loading = NO;
    if (generation != _generation) {
        // what is shown changed while this was being made: it is made again, but only while the screen is up
        _shownRevision = (NSUInteger)-1;
        if (_timer)
            [self reload];
        return;
    }
    _shownRevision = revision;
    CGFloat distance = _table.contentSize.height - _table.contentOffset.y - _table.bounds.size.height;
    BOOL follow = _pinned || distance < 48;
    [_rows release];
    _rows = [rows copy];
    [_rowLevels release];
    _rowLevels = [levels copy];
    [_rowHeights release];
    _rowHeights = [heights copy];
    [_table reloadData];
    if (follow && [_rows count] > 1)
        [_table scrollToRowAtIndexPath:[NSIndexPath indexPathForRow:(NSInteger)[_rows count] - 1 inSection:0] atScrollPosition:UITableViewScrollPositionBottom animated:NO];
    _subtitleLabel.text = [NSString stringWithFormat:@"%@ · %lu", state, (unsigned long)[_rows count]];
    _pinned = NO;
}

#pragma mark rows

- (CGFloat)heightOfRow:(NSUInteger)index {
    return index < [_rowHeights count] ? (CGFloat)[[_rowHeights objectAtIndex:index] doubleValue] : 0;
}

- (NSInteger)tableView:(UITableView *)table numberOfRowsInSection:(NSInteger)section {
    return (NSInteger)[_rows count];
}

- (CGFloat)tableView:(UITableView *)table heightForRowAtIndexPath:(NSIndexPath *)path {
    return [self heightOfRow:(NSUInteger)path.row];
}

- (UIColor *)colorOfLevel:(int)level {
    switch (level) {
    case 0: return [YZTheme subtleTextColor];
    case 2: return [UIColor colorWithRed:0.86f green:0.52f blue:0.08f alpha:1];
    case 3: return [UIColor colorWithRed:0.84f green:0.2f blue:0.2f alpha:1];
    default: return [YZTheme textColor];
    }
}

- (UITableViewCell *)tableView:(UITableView *)table cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [table dequeueReusableCellWithIdentifier:YZLogRowIdentifier];
    UILabel *label;
    if (!cell) {
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:YZLogRowIdentifier] autorelease];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        cell.backgroundColor = [UIColor clearColor];
        label = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        label.tag = YZLogRowLabelTag;
        label.backgroundColor = [UIColor clearColor];
        label.font = YZLogFont(11);
        label.numberOfLines = (NSInteger)YZLogRowLines;
        label.lineBreakMode = YZLineBreakTailTruncation;
        [cell.contentView addSubview:label];
    } else
        label = (UILabel *)[cell.contentView viewWithTag:YZLogRowLabelTag];
    NSUInteger index = (NSUInteger)path.row;
    if (index < [_rows count]) {
        label.text = [_rows objectAtIndex:index];
        label.textColor = [self colorOfLevel:[[_rowLevels objectAtIndex:index] intValue]];
    }
    label.frame = CGRectMake(YZLogRowSide, 0, MAX((CGFloat)0, table.bounds.size.width - 2 * YZLogRowSide), [self heightOfRow:index]);
    return cell;
}

- (void)copyLog {
    NSString *text = [YZLog textForSource:(YZLogSource)_source minimumLevel:(int)_level lines:0];
    if ([text length])
        [UIPasteboard generalPasteboard].string = text;
    [YZTheme animateFeedbackOfView:_copy];
}

- (void)clearLog {
    [YZLog clearSource:(YZLogSource)_source];
    [YZTheme animateFeedbackOfView:_clear];
    ++_generation;
    _shownRevision = (NSUInteger)-1;
    [self reload];
}

@end
