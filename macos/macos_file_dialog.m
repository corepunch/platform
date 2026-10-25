#import <AppKit/AppKit.h>
#import <Security/SecTask.h>
#include "../platform.h"

bool_t axFileDialogsRequireNative(void) {
  static int required = -1;
  if (required >= 0) return required;
  SecTaskRef task = SecTaskCreateFromSelf(NULL);
  if (!task) {
    fprintf(stderr, "[macos] file dialog: cannot inspect sandbox entitlement\n");
    fflush(stderr);
    return TRUE;
  }
  CFErrorRef error = NULL;
  CFTypeRef value = SecTaskCopyValueForEntitlement(task, CFSTR("com.apple.security.app-sandbox"), &error);
  required = value && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(value);
  if (value && CFGetTypeID(value) != CFBooleanGetTypeID()) {
    fprintf(stderr, "[macos] file dialog: sandbox entitlement is not Boolean type=%lu\n", CFGetTypeID(value));
    fflush(stderr);
    required = TRUE;
  }
  if (error) {
    fprintf(stderr, "[macos] file dialog: sandbox entitlement query failed code=%ld\n", (long)CFErrorGetCode(error));
    fflush(stderr);
    CFRelease(error);
    required = TRUE;
  }
  if (value) CFRelease(value);
  CFRelease(task);
  return required;
}

@interface AXFilePanelFilters : NSObject {
  NSSavePanel *_panel;
  NSArray *_patterns;
}
- (id)initWithPanel:(NSSavePanel *)panel request:(AXopenfilename const *)ofn;
- (void)selectFilter:(NSPopUpButton *)popup;
@end

@implementation AXFilePanelFilters
- (id)initWithPanel:(NSSavePanel *)panel request:(AXopenfilename const *)ofn {
  if (!(self = [super init])) return nil;
  _panel = panel;
  NSMutableArray *patterns = [NSMutableArray array];
  NSPopUpButton *popup = [[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 300, 26) pullsDown:NO] autorelease];
  for (const char *p = ofn->lpstrFilter; p && *p;) {
    NSString *label = [NSString stringWithUTF8String:p];
    p += strlen(p) + 1;
    if (!*p) break;
    NSString *pattern = [NSString stringWithUTF8String:p];
    if (label && pattern) {
      [popup addItemWithTitle:label];
      [patterns addObject:pattern];
    }
    p += strlen(p) + 1;
  }
  _patterns = [patterns copy];
  if (_patterns.count) {
    NSUInteger index = ofn->nFilterIndex > 0 ? ofn->nFilterIndex - 1 : 0;
    [popup selectItemAtIndex:index < _patterns.count ? index : 0];
    [self selectFilter:popup];
    if (_patterns.count > 1) {
      popup.target = self;
      popup.action = @selector(selectFilter:);
      panel.accessoryView = popup;
    }
  }
  return self;
}
- (void)selectFilter:(NSPopUpButton *)popup {
  NSMutableArray *extensions = [NSMutableArray array];
  for (NSString *pattern in [_patterns[popup.indexOfSelectedItem] componentsSeparatedByString:@";"]) {
    NSString *part = [pattern stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
    if ([part isEqualToString:@"*"] || [part isEqualToString:@"*.*"]) { [extensions removeAllObjects]; break; }
    if ([part hasPrefix:@"*."] && part.length > 2) [extensions addObject:[part substringFromIndex:2]];
  }
  _panel.allowedFileTypes = extensions.count ? extensions : nil;
  _panel.allowsOtherFileTypes = extensions.count == 0;
}
- (void)dealloc {
  [_patterns release];
  [super dealloc];
}
@end

static bool_t macos_file_dialog(AXopenfilename const *ofn, bool_t save, bool_t folder) {
  if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile || !memchr(ofn->lpstrFile, 0, ofn->nMaxFile)) {
    fprintf(stderr, "[macos] file dialog rejected request=%p buffer=%p size=%u\n",
      (void *)ofn, ofn ? (void *)ofn->lpstrFile : NULL, ofn ? ofn->nMaxFile : 0);
    fflush(stderr);
    return FALSE;
  }
  if (![NSThread isMainThread]) {
    fprintf(stderr, "[macos] file dialog rejected: must run on the main thread\n");
    fflush(stderr);
    return FALSE;
  }
  @autoreleasepool {
    NSSavePanel *panel = save ? [NSSavePanel savePanel] : [NSOpenPanel openPanel];
    if (!panel) {
      fprintf(stderr, "[macos] file dialog: cannot allocate panel save=%u folder=%u\n", save, folder);
      fflush(stderr);
      return FALSE;
    }
    if (!save) {
      NSOpenPanel *open = (NSOpenPanel *)panel;
      open.canChooseFiles = !folder;
      open.canChooseDirectories = folder;
      open.allowsMultipleSelection = NO;
    }
    if (ofn->lpstrTitle) panel.title = [NSString stringWithUTF8String:ofn->lpstrTitle];
    NSString *initial = [NSString stringWithUTF8String:ofn->lpstrFile];
    if (!initial) {
      fprintf(stderr, "[macos] file dialog rejected: initial path is not UTF-8\n");
      fflush(stderr);
      return FALSE;
    }
    if (initial.length) {
      initial = [initial stringByExpandingTildeInPath];
      if (folder) panel.directoryURL = [NSURL fileURLWithPath:initial isDirectory:YES];
      else {
        NSString *directory = [initial stringByDeletingLastPathComponent];
        if (directory.length) panel.directoryURL = [NSURL fileURLWithPath:directory isDirectory:YES];
        panel.nameFieldStringValue = initial.lastPathComponent;
      }
    }
    panel.canCreateDirectories = save || folder;
    AXFilePanelFilters *filters = folder ? nil : [[AXFilePanelFilters alloc] initWithPanel:panel request:ofn];
    if (!folder && !filters) {
      fprintf(stderr, "[macos] file dialog: cannot allocate file filters\n");
      fflush(stderr);
      return FALSE;
    }
    NSInteger result = [panel runModal];
    panel.accessoryView = nil;
    [filters release];
    if (result != NSModalResponseOK) return FALSE;
    // Powerbox grants access before returning; callers may save again after this panel closes.
    const char *path = panel.URL.fileSystemRepresentation;
    if (!path || strlen(path) >= ofn->nMaxFile) {
      fprintf(stderr, "[macos] file dialog rejected selected path: length=%zu capacity=%u\n",
        path ? strlen(path) : 0, ofn->nMaxFile);
      fflush(stderr);
      [panel.URL stopAccessingSecurityScopedResource];
      return FALSE;
    }
    strcpy(ofn->lpstrFile, path);
    return TRUE;
  }
}

bool_t axGetOpenFileName(AXopenfilename const *ofn) { return macos_file_dialog(ofn, FALSE, FALSE); }
bool_t axGetSaveFileName(AXopenfilename const *ofn) { return macos_file_dialog(ofn, TRUE,  FALSE); }
bool_t axGetFolderName(AXopenfilename const *ofn)   { return macos_file_dialog(ofn, FALSE, TRUE); }
