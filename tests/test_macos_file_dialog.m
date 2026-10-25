#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <assert.h>
#include "../macos/macos_file_dialog.m"

@interface AXTestFilePanel : NSObject
@property(copy) NSString *title;
@property(copy) NSString *nameFieldStringValue;
@property(retain) NSURL *directoryURL;
@property(retain) NSURL *URL;
@property(retain) NSView *accessoryView;
@property(copy) NSArray *allowedFileTypes;
@property BOOL allowsOtherFileTypes, canCreateDirectories, canChooseFiles, canChooseDirectories, allowsMultipleSelection;
@property NSInteger response;
@property void (*check)(AXTestFilePanel *);
- (NSInteger)runModal;
@end

@implementation AXTestFilePanel
- (NSInteger)runModal { if (self.check) self.check(self); return self.response; }
- (void)dealloc {
  [_title release]; [_nameFieldStringValue release]; [_directoryURL release];
  [_URL release]; [_accessoryView release]; [_allowedFileTypes release];
  [super dealloc];
}
@end

static AXTestFilePanel *test_panel;
static id test_panel_factory(id self, SEL command) { (void)self; (void)command; return test_panel; }

static void check_open(AXTestFilePanel *panel) {
  assert(panel.canChooseFiles && !panel.canChooseDirectories && !panel.allowsMultipleSelection);
  assert([panel.title isEqualToString:@"Open song"]);
  assert([panel.nameFieldStringValue isEqualToString:@"Mélodie.groove"]);
  assert([panel.directoryURL.path isEqualToString:@"/tmp/Songs"]);
  assert(([panel.allowedFileTypes isEqualToArray:@[@"groove", @"wav"]]));
  NSPopUpButton *popup = (NSPopUpButton *)panel.accessoryView;
  assert(popup.numberOfItems == 2);
  [popup selectItemAtIndex:1];
  [popup.target selectFilter:popup];
  assert(!panel.allowedFileTypes && panel.allowsOtherFileTypes);
}

static void check_save(AXTestFilePanel *panel) {
  assert([panel.nameFieldStringValue isEqualToString:@"Untitled.groove"]);
  assert(panel.canCreateDirectories);
  assert([panel.allowedFileTypes isEqualToArray:@[@"groove"]]);
  assert(!panel.allowsOtherFileTypes);
}

static void check_folder(AXTestFilePanel *panel) {
  assert(!panel.canChooseFiles && panel.canChooseDirectories && !panel.allowsMultipleSelection);
  assert([panel.directoryURL.path isEqualToString:@"/tmp/Songs"]);
  assert(!panel.accessoryView && !panel.allowedFileTypes);
}

static void reset_panel(void) {
  [test_panel release];
  test_panel = [AXTestFilePanel new];
  test_panel.response = NSModalResponseOK;
  test_panel.URL = [NSURL fileURLWithPath:@"/tmp/External/Mélodie.groove"];
}

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc == 2) {
      assert(axFileDialogsRequireNative() == (bool_t)atoi(argv[1]));
      puts("macOS signed sandbox detection passed.");
      return 0;
    }
    [NSApplication sharedApplication];
    Method open = class_getClassMethod([NSOpenPanel class], @selector(openPanel));
    Method save = class_getClassMethod([NSSavePanel class], @selector(savePanel));
    IMP old_open = method_setImplementation(open, (IMP)test_panel_factory);
    IMP old_save = method_setImplementation(save, (IMP)test_panel_factory);
    char path[256] = "/tmp/Songs/Mélodie.groove";
    AXopenfilename ofn = { .lpstrFile = path, .nMaxFile = sizeof(path),
      .lpstrFilter = "Songs\0*.groove;*.wav\0All Files\0*.*\0", .lpstrTitle = "Open song" };
    reset_panel(); test_panel.check = check_open;
    assert(axGetOpenFileName(&ofn));
    assert(!strcmp(path, test_panel.URL.fileSystemRepresentation));

    reset_panel(); test_panel.check = check_save;
    strcpy(path, "Untitled.groove");
    ofn.lpstrFilter = "All Files\0*.*\0Songs\0*.groove\0";
    ofn.nFilterIndex = 2;
    assert(axGetSaveFileName(&ofn));

    reset_panel(); test_panel.check = check_folder;
    strcpy(path, "/tmp/Songs");
    assert(axGetFolderName(&ofn));

    reset_panel(); test_panel.response = NSModalResponseCancel;
    strcpy(path, "Original.groove");
    assert(!axGetOpenFileName(&ofn));
    assert(!strcmp(path, "Original.groove"));
    assert(!axGetSaveFileName(&ofn));
    assert(!strcmp(path, "Original.groove"));

    reset_panel();
    char small[5] = "old";
    ofn.lpstrFile = small; ofn.nMaxFile = sizeof(small);
    assert(!axGetSaveFileName(&ofn));
    assert(!strcmp(small, "old"));
    assert(!axGetOpenFileName(NULL));
    ofn.nMaxFile = 0;
    assert(!axGetOpenFileName(&ofn));
    memset(small, 'x', sizeof(small)); ofn.nMaxFile = sizeof(small);
    assert(!axGetOpenFileName(&ofn));
    assert(small[4] == 'x');
    method_setImplementation(open, old_open);
    method_setImplementation(save, old_save);
    [test_panel release];
    puts("macOS native file dialog tests passed.");
  }
  return 0;
}
