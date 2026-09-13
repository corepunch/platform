#include "ios_local.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

char const *axGetPlatform(void) { return "ios"; }
char const *axShareDirectory(void) { return NSBundle.mainBundle.resourcePath.fileSystemRepresentation; }
char const *axLibDirectory(void) { return axShareDirectory(); }
char const *axSettingsDirectory(void) {
  static char path[4096];
  if (!path[0]) {
    NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSError *error = nil;
    if (![NSFileManager.defaultManager createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:&error]) {
      IOS_TRACE("settings directory failed: %s", error.localizedDescription.UTF8String); return "";
    }
    snprintf(path, sizeof(path), "%s", url.fileSystemRepresentation);
  }
  return path;
}
bool_t axIsDarkTheme(void) { return ios_view.traitCollection.userInterfaceStyle == UIUserInterfaceStyleDark; }
void axSetCursor(int cursor) { /* UIKit owns pointer appearance. */ }
bool_t axJoystickInit(void) { return FALSE; }
void axJoystickShutdown(void) {}
bool_t axJoystickAvailable(void) { return FALSE; }
char const *axJoystickGetName(void) { return NULL; }

@interface AXDocumentPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) BOOL done;
@property(nonatomic, copy) NSString *path;
@end
@implementation AXDocumentPicker
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller { self.done = YES; IOS_TRACE("document picker cancelled"); }
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  NSURL *url = urls.firstObject;
  self.path = url.path;
  self.done = YES;
  IOS_TRACE("document selected path=%s", self.path.UTF8String);
}
@end

static bool_t ios_pick_file(AXopenfilename const *ofn, bool_t folder) {
  if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile || !ios_window || ios_window.rootViewController.presentedViewController) {
    IOS_TRACE("file picker rejected: invalid request or active modal"); return FALSE;
  }
  AXDocumentPicker *delegate = [AXDocumentPicker new];
  UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[folder ? UTTypeFolder : UTTypeData] asCopy:!folder];
  picker.delegate = delegate;
  [ios_window.rootViewController presentViewController:picker animated:YES completion:nil];
  IOS_TRACE("open document picker folder=%u", folder);
  while (!delegate.done) axWaitMessage(10);
  if (!delegate.path || strlen(delegate.path.fileSystemRepresentation) >= ofn->nMaxFile) {
    if (delegate.path) IOS_TRACE("selected path exceeds capacity=%u", ofn->nMaxFile);
    return FALSE;
  }
  strcpy(ofn->lpstrFile, delegate.path.fileSystemRepresentation);
  return TRUE;
}
bool_t axGetOpenFileName(AXopenfilename const *ofn) { return ios_pick_file(ofn, FALSE); }
bool_t axGetFolderName(AXopenfilename const *ofn) {
  IOS_TRACE("external folder access unsupported; use app Documents"); return FALSE;
}
bool_t axGetSaveFileName(AXopenfilename const *ofn) {
  if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile || !ios_window || ios_window.rootViewController.presentedViewController) {
    IOS_TRACE("save picker rejected: invalid request or active modal"); return FALSE;
  }
  __block BOOL done = NO;
  __block NSString *path = nil;
  UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Save in Documents" message:nil preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
    field.text = ofn->lpstrFile[0] ? [NSString stringWithUTF8String:ofn->lpstrFile].lastPathComponent : @"Untitled.png";
  }];
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) { done = YES; }]];
  [alert addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
    NSString *name = alert.textFields.firstObject.text.lastPathComponent;
    if (name.length && ![name isEqualToString:@"."] && ![name isEqualToString:@".."]) {
      NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
      path = [documents stringByAppendingPathComponent:name];
    }
    done = YES;
  }]];
  [ios_window.rootViewController presentViewController:alert animated:YES completion:nil];
  while (!done) axWaitMessage(10);
  if (!path || strlen(path.fileSystemRepresentation) >= ofn->nMaxFile) return FALSE;
  strcpy(ofn->lpstrFile, path.fileSystemRepresentation);
  IOS_TRACE("save selected path=%s", ofn->lpstrFile);
  return TRUE;
}

void axSetTextInput(bool_t enabled) {
  IOS_TRACE("text input enabled=%u", enabled);
  if (enabled) [ios_view becomeFirstResponder];
  else { [ios_view resignFirstResponder]; [ios_window.rootViewController becomeFirstResponder]; }
}
