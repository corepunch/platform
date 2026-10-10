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
void axSetCursor(int cursor) { (void)cursor; /* UIKit owns pointer appearance. */ }
bool_t axJoystickInit(void) { return FALSE; }
void axJoystickShutdown(void) {}
bool_t axJoystickAvailable(void) { return FALSE; }
char const *axJoystickGetName(void) { return NULL; }

static NSURL *ios_documents_url(void) {
  NSURL *url = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
  NSError *error = nil;
  if (!url || ![NSFileManager.defaultManager createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:&error]) {
    IOS_TRACE("Documents unavailable: %s", error.localizedDescription.UTF8String); return nil;
  }
  return url;
}

// "name", then "name 2", "name 3", ... starting at the given attempt.
static NSURL *ios_unique_document(NSURL *documents, NSString *name, int attempt) {
  NSURL *destination = nil;
  for (int suffix = attempt; !destination || [NSFileManager.defaultManager fileExistsAtPath:destination.path]; suffix++) {
    NSString *stem = name;
    if (suffix > 1) {
      stem = [NSString stringWithFormat:@"%@ %d", name.stringByDeletingPathExtension, suffix];
      if (name.pathExtension.length) stem = [stem stringByAppendingPathExtension:name.pathExtension];
    }
    destination = [documents URLByAppendingPathComponent:stem];
  }
  return destination;
}

// Files dragged in from other apps are copied into Documents, then posted as
// kEventDragDrop at the drop point, one event per file.
@interface AXDropDelegate : NSObject <UIDropInteractionDelegate>
@end
@implementation AXDropDelegate
- (BOOL)dropInteraction:(UIDropInteraction *)interaction canHandleSession:(id<UIDropSession>)session {
  return [session hasItemsConformingToTypeIdentifiers:@[UTTypeData.identifier]];
}
- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction sessionDidUpdate:(id<UIDropSession>)session {
  return [[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy];
}
- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session {
  CGPoint at = [session locationInView:interaction.view];
  NSURL *documents = ios_documents_url();
  if (!documents) return;
  for (UIDragItem *item in session.items) {
    NSItemProvider *provider = item.itemProvider;
    NSString *type = nil;
    for (NSString *candidate in provider.registeredTypeIdentifiers)
      if ([[UTType typeWithIdentifier:candidate] conformsToType:UTTypeData]) { type = candidate; break; }
    if (!type) { IOS_TRACE("drop item has no file representation types=%s", provider.registeredTypeIdentifiers.description.UTF8String); continue; }
    [provider loadFileRepresentationForTypeIdentifier:type completionHandler:^(NSURL *url, NSError *error) {
      if (!url) { IOS_TRACE("drop load failed type=%s: %s", type.UTF8String, error.localizedDescription.UTF8String); return; }
      // The provider's file is deleted when this handler returns; concurrent drops may race for a name.
      NSError *copy_error = nil;
      NSURL *destination = nil;
      for (int attempt = 1; attempt < 100; attempt++) {
        destination = ios_unique_document(documents, url.lastPathComponent, attempt);
        copy_error = nil;
        if ([NSFileManager.defaultManager copyItemAtURL:url toURL:destination error:&copy_error]) break;
        if (copy_error.code != NSFileWriteFileExistsError) break;
      }
      if (copy_error) { IOS_TRACE("drop import failed name=%s: %s", url.lastPathComponent.UTF8String, copy_error.localizedDescription.UTF8String); return; }
      axNotifyFileDropEvent(destination.fileSystemRepresentation, (float)at.x, (float)at.y);
    }];
  }
}
@end

void ios_install_drop(UIView *view) {
  static AXDropDelegate *delegate;   // UIDropInteraction holds its delegate weakly
  if (!delegate) delegate = [AXDropDelegate new];
  [view addInteraction:[[UIDropInteraction alloc] initWithDelegate:delegate]];
}

static void ios_wait_for_dismissal(void) {
  while (ios_window.rootViewController.presentedViewController) axWaitMessage(10);
}

static void ios_file_error(NSString *message) {
  IOS_TRACE("file operation failed: %s", message.UTF8String);
  __block BOOL done = NO;
  UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"File operation failed" message:message preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) { (void)action; done = YES; }]];
  [ios_window.rootViewController presentViewController:alert animated:YES completion:nil];
  while (!done) axWaitMessage(10);
  ios_wait_for_dismissal();
}

@interface AXDocumentPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) BOOL done;
@property(nonatomic, strong) NSURL *url;
@end
@implementation AXDocumentPicker
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller { self.done = YES; IOS_TRACE("document picker cancelled"); }
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  self.url = urls.firstObject;
  self.done = YES;
}
@end

bool_t axGetOpenFileName(AXopenfilename const *ofn) {
  if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile || !ios_window || ios_window.rootViewController.presentedViewController) {
    IOS_TRACE("file picker rejected: invalid request or active modal"); return FALSE;
  }
  NSURL *documents = ios_documents_url();
  if (!documents) return FALSE;
  AXDocumentPicker *delegate = [AXDocumentPicker new];
  UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:NO];
  picker.directoryURL = documents;
  picker.allowsMultipleSelection = NO;
  picker.delegate = delegate;
  [ios_window.rootViewController presentViewController:picker animated:YES completion:nil];
  IOS_TRACE("open document picker in Documents");
  while (!delegate.done) axWaitMessage(10);
  ios_wait_for_dismissal();
  NSURL *source = delegate.url;
  if (!source) return FALSE;
  BOOL scoped = [source startAccessingSecurityScopedResource];
  NSURL *destination = source;
  NSString *root = [documents.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
  BOOL local = [source.URLByResolvingSymlinksInPath.path hasPrefix:root];
  BOOL ok = YES;
  // Import external providers into permanent, app-owned storage without replacing another drawing.
  if (!local) destination = ios_unique_document(documents, source.lastPathComponent, 1);
  if (strlen(destination.fileSystemRepresentation) >= ofn->nMaxFile) {
    ios_file_error(@"The selected filename is too long."); ok = NO;
  } else if (!local) {
    __block NSError *copy_error = nil;
    NSError *coord_error = nil;
    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateReadingItemAtURL:source options:0 error:&coord_error byAccessor:^(NSURL *url) {
      [NSFileManager.defaultManager copyItemAtURL:url toURL:destination error:&copy_error];
    }];
    if (coord_error || copy_error) { ios_file_error((coord_error ?: copy_error).localizedDescription); ok = NO; }
  }
  if (scoped) [source stopAccessingSecurityScopedResource];
  if (!ok) return FALSE;
  strcpy(ofn->lpstrFile, destination.fileSystemRepresentation);
  IOS_TRACE("open selected path=%s imported=%d", ofn->lpstrFile, !local);
  return TRUE;
}

bool_t axGetFolderName(AXopenfilename const *ofn) {
  (void)ofn;
  IOS_TRACE("external folder access unsupported; use app Documents"); return FALSE;
}

bool_t axGetSaveFileName(AXopenfilename const *ofn) {
  if (!ofn || !ofn->lpstrFile || !ofn->nMaxFile || !ios_window || ios_window.rootViewController.presentedViewController) {
    IOS_TRACE("save picker rejected: invalid request or active modal"); return FALSE;
  }
  NSURL *documents = ios_documents_url();
  if (!documents) return FALSE;
  NSString *suggested = ofn->lpstrFile[0] ? [NSString stringWithUTF8String:ofn->lpstrFile].lastPathComponent : @"Untitled";
  NSString *extension = suggested.pathExtension;
  if (ofn->lpstrFilter) {
    const char *pattern = ofn->lpstrFilter + strlen(ofn->lpstrFilter) + 1;
    if (pattern[0] == '*' && pattern[1] == '.' && pattern[2] && pattern[2] != '*')
      extension = [[NSString stringWithUTF8String:pattern + 2] componentsSeparatedByString:@";"].firstObject;
  }
  __block BOOL done = NO;
  __block NSString *name = nil;
  UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Save in Documents" message:@"Choose a filename. Your document will appear in the Files app." preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
    field.text = suggested;
    field.autocapitalizationType = UITextAutocapitalizationTypeNone;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
  }];
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) { (void)action; done = YES; }]];
  [alert addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
    (void)action; name = [alert.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]; done = YES;
  }]];
  [ios_window.rootViewController presentViewController:alert animated:YES completion:nil];
  IOS_TRACE("save dialog default=%s", suggested.UTF8String);
  while (!done) axWaitMessage(10);
  ios_wait_for_dismissal();
  if (!name) { IOS_TRACE("save cancelled"); return FALSE; }
  if (!name.length || [name isEqualToString:@"."] || [name isEqualToString:@".."] ||
      [name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/:"]].location != NSNotFound) {
    ios_file_error(@"Enter a filename without slashes or colons."); return FALSE;
  }
  if (extension.length && [name.pathExtension caseInsensitiveCompare:extension] != NSOrderedSame)
    name = [name stringByAppendingPathExtension:extension];
  NSURL *url = [documents URLByAppendingPathComponent:name];
  if (strlen(url.fileSystemRepresentation) >= ofn->nMaxFile) { ios_file_error(@"The filename is too long."); return FALSE; }
  BOOL directory = NO;
  if ([NSFileManager.defaultManager fileExistsAtPath:url.path isDirectory:&directory]) {
    if (directory) { ios_file_error(@"A folder already has this name."); return FALSE; }
    __block BOOL replace = NO;
    done = NO;
    UIAlertController *confirm = [UIAlertController alertControllerWithTitle:@"Replace document?" message:name preferredStyle:UIAlertControllerStyleAlert];
    [confirm addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action) { (void)action; done = YES; }]];
    [confirm addAction:[UIAlertAction actionWithTitle:@"Replace" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) { (void)action; replace = YES; done = YES; }]];
    [ios_window.rootViewController presentViewController:confirm animated:YES completion:nil];
    while (!done) axWaitMessage(10);
    ios_wait_for_dismissal();
    IOS_TRACE("overwrite path=%s confirmed=%d", url.fileSystemRepresentation, replace);
    if (!replace) return FALSE;
  }
  strcpy(ofn->lpstrFile, url.fileSystemRepresentation);
  IOS_TRACE("save selected path=%s", ofn->lpstrFile);
  return TRUE;
}

void axSetTextInput(bool_t enabled) {
  IOS_TRACE("text input enabled=%u", enabled);
  if (enabled) [ios_view becomeFirstResponder];
  else { [ios_view resignFirstResponder]; [ios_window.rootViewController becomeFirstResponder]; }
}
