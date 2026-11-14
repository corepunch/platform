#import <AppKit/AppKit.h>

#include "macos_local.h"

/* The application menu bar, the title bar that hides behind the content, and
 * dragging the window from that content. */

NSEvent *macos_last_mouse_down;

@interface MenuTarget : NSObject
@end

@implementation MenuTarget
- (void)perform:(NSMenuItem *)sender
{
  axPostMessageW(NULL, kEventMenuCommand, (uint32_t)sender.tag, NULL);
}
- (void)quit:(id)sender
{
  /* The event loop decides, as it does for the close button. */
  axPostMessageW(NULL, kEventWindowClosed, 0, NULL);
}
@end

static MenuTarget *menu_target;

static NSString *
KeyEquivalent(uint32_t key)
{
  static const unichar special[][2] = {
    { AX_KEY_UPARROW, NSUpArrowFunctionKey },     { AX_KEY_DOWNARROW, NSDownArrowFunctionKey },
    { AX_KEY_LEFTARROW, NSLeftArrowFunctionKey }, { AX_KEY_RIGHTARROW, NSRightArrowFunctionKey },
    { AX_KEY_DEL, NSDeleteFunctionKey },          { AX_KEY_INS, NSInsertFunctionKey },
    { AX_KEY_HOME, NSHomeFunctionKey },           { AX_KEY_END, NSEndFunctionKey },
    { AX_KEY_PGUP, NSPageUpFunctionKey },         { AX_KEY_PGDN, NSPageDownFunctionKey },
    { AX_KEY_BACKSPACE, NSBackspaceCharacter },   { AX_KEY_ENTER, NSCarriageReturnCharacter },
    { AX_KEY_TAB, NSTabCharacter },               { AX_KEY_ESCAPE, 0x1b },
  };
  unichar c = 0;
  if (key >= AX_KEY_F1 && key <= AX_KEY_F12) c = (unichar)(NSF1FunctionKey + (key - AX_KEY_F1));
  else if (key >= AX_KEY_A && key <= AX_KEY_Z) c = (unichar)(key - AX_KEY_A + 'a');
  else if (key > 32 && key < 127) c = (unichar)key;
  for (size_t i = 0; !c && i < sizeof(special) / sizeof(special[0]); i++)
    if (special[i][0] == key) c = special[i][1];
  return c ? [NSString stringWithCharacters:&c length:1] : @"";
}

static NSMenu *
BuildMenu(NSString *title, const ax_menu_item_t *items, uint32_t count)
{
  NSMenu *menu = [[[NSMenu alloc] initWithTitle:title] autorelease];
  menu.autoenablesItems = NO;
  for (uint32_t i = 0; i < count; i++) {
    const ax_menu_item_t *it = &items[i];
    if (!it->title) {
      [menu addItem:[NSMenuItem separatorItem]];
      continue;
    }
    NSString *name = [NSString stringWithUTF8String:it->title];
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:name ? name : @"" action:nil keyEquivalent:@""] autorelease];
    if (it->items && it->count) {
      item.submenu = BuildMenu(item.title, it->items, it->count);
    } else {
      NSEventModifierFlags mask = 0;
      if (it->modifiers & AX_MOD_SHIFT) mask |= NSEventModifierFlagShift;
      if (it->modifiers & AX_MOD_CTRL) mask |= NSEventModifierFlagControl;
      if (it->modifiers & AX_MOD_ALT) mask |= NSEventModifierFlagOption;
      if (it->modifiers & AX_MOD_CMD) mask |= NSEventModifierFlagCommand;
      item.target = menu_target;
      item.action = @selector(perform:);
      item.tag = (NSInteger)it->command;
      item.keyEquivalent = KeyEquivalent(it->key);
      item.keyEquivalentModifierMask = mask;
    }
    [menu addItem:item];
  }
  return menu;
}

bool_t
axSetApplicationMenu(const ax_menu_item_t *menus, uint32_t count)
{
  if (!NSApp || (count && !menus)) return FALSE;
  if (!menu_target) menu_target = [[MenuTarget alloc] init];
  NSMenu *bar = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  /* The first menu is the application's own, whatever its title says. */
  NSString *app = [[NSProcessInfo processInfo] processName];
  NSMenu *app_menu = [[[NSMenu alloc] initWithTitle:app] autorelease];
  [app_menu addItemWithTitle:[@"Hide " stringByAppendingString:app] action:@selector(hide:) keyEquivalent:@"h"];
  [app_menu addItem:[NSMenuItem separatorItem]];
  NSMenuItem *quit = [app_menu addItemWithTitle:[@"Quit " stringByAppendingString:app] action:@selector(quit:) keyEquivalent:@"q"];
  quit.target = menu_target;
  NSMenuItem *app_item = [[[NSMenuItem alloc] initWithTitle:app action:nil keyEquivalent:@""] autorelease];
  app_item.submenu = app_menu;
  [bar addItem:app_item];
  for (uint32_t i = 0; i < count; i++) {
    if (!menus[i].title) continue;
    NSString *title = [NSString stringWithUTF8String:menus[i].title];
    NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:title ? title : @"" action:nil keyEquivalent:@""] autorelease];
    item.submenu = BuildMenu(item.title, menus[i].items, menus[i].count);
    [bar addItem:item];
  }
  [NSApp setMainMenu:bar];
  return TRUE;
}

bool_t
axSetTitlebarHidden(bool_t hidden, uint32_t *controls_width, uint32_t *controls_height)
{
  NSWindow *window = [NSApp mainWindow] ? [NSApp mainWindow] : [[NSApp windows] firstObject];
  if (controls_width) *controls_width = 0;
  if (controls_height) *controls_height = 0;
  if (!window || !(window.styleMask & NSWindowStyleMaskTitled)) return FALSE;
  NSSize content = window.contentView.frame.size;
  if (hidden) window.styleMask |= NSWindowStyleMaskFullSizeContentView;
  else window.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
  window.titlebarAppearsTransparent = hidden ? YES : NO;
  window.titleVisibility = hidden ? NSWindowTitleHidden : NSWindowTitleVisible;
  /* The content keeps its size: the window gives the title bar's height back, or takes it. */
  [window setContentSize:content];
  NSButton *last = [window standardWindowButton:NSWindowZoomButton];
  NSButton *first = [window standardWindowButton:NSWindowCloseButton];
  if (hidden && first && last) {
    NSRect lead = [first convertRect:first.bounds toView:nil], trail = [last convertRect:last.bounds toView:nil];
    if (controls_width) *controls_width = (uint32_t)(NSMaxX(trail) + NSMinX(lead));
    if (controls_height) *controls_height = (uint32_t)(window.frame.size.height - window.contentLayoutRect.size.height);
  }
  return TRUE;
}

bool_t
axDragWindow(void)
{
  NSEvent *down = macos_last_mouse_down;
  if (!down || !down.window) return FALSE;
  [down.window performWindowDragWithEvent:down];
  return TRUE;
}
