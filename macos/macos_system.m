#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>

#include "macos_local.h"

#include <TargetConditionals.h>

extern bool_t g_refreshAssets;
NSString *appName = @"CorePunch";

static char documents[1024] = { 0 };
static char local[1024] = { 0 };
static char share[1024] = { 0 };

//void Menu_Command(uint32_t eventType);
//void INP_NullEvent(void);

bool_t __openFile = FALSE;
lpcString_t  __openFileType = NULL;
void (*__openProjectFile)(lpcString_t );

void SYS_OpenFile(lpcString_t  ext, void (*callback)(lpcString_t )) {
	__openFile = TRUE;
	__openFileType = ext;
	__openProjectFile = callback;
}

void SYS_OpenFolder(void (*callback)(lpcString_t )) {
	__openFile = TRUE;
	__openFileType = NULL;
	__openProjectFile = callback;
}

lpcString_t  SYS_DocumentsDirectory(void) {
    if (documents[0] == 0) {
        NSFileManager *fileManager = [NSFileManager defaultManager];
        NSArray<NSURL*>* urls = [fileManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask];
        NSString *path = [urls[0] path];
        memcpy(documents, [path UTF8String], MIN([path length] + 1, sizeof(documents)));
    }
	return documents;
}

lpcString_t  SYS_SettingsDirectory(void) {
    if (local[0] == 0) {
        NSFileManager *fileManager = [NSFileManager defaultManager];
        NSArray<NSURL*>* urls = [fileManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
        NSString *applicationSupportDirectory = [urls[0] path];
        NSString *path = [applicationSupportDirectory stringByAppendingPathComponent:appName];
        NSError *errorw;
        
        [fileManager createDirectoryAtPath:path
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&errorw];

        memcpy(local, [path UTF8String], MIN([path length] + 1, sizeof(local)));
    }
	return local;
}


lpcString_t  SYS_ShareDirectory(void) {
    if (share[0] == 0) {
        unsigned size = sizeof(share);
        _NSGetExecutablePath(share, &size);
        LPSTR c = share + strlen(share) - 1;
#if !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
        lpcString_t  resource = "/Resources/";
        for (int i = 1;; c--) {
            if (*c == '/') {
                if (i > 0){
                    i--;
                } else {
                    break;
                }
            }
        }
        strcpy(c, resource);
#else
        for (; *c != '/'; c--);
        *(c + 1) = 0;
#endif
    }
    return share;
}

lpcString_t  SYS_LibDirectory(void) {
    return SYS_ShareDirectory();
}

void SYS_SetDragContents(lpcString_t  contents) {
}

lpcString_t SYS_GetPlatform(void) {
    return "macos";
}

//void SYS_GetOpenFileName(void (*callback)(lpcString_t)) {
//    // Create and configure the open panel
//    NSOpenPanel *panel = [NSOpenPanel openPanel];
//    [panel setCanChooseFiles:NO];
//    [panel setCanChooseDirectories:YES];
//    [panel setAllowsMultipleSelection:NO];
//    [panel setResolvesAliases:YES];
//    [panel setTitle:@"Select a Folder"];
//    [panel setMessage:@"Please select a folder."];
//    
//    // Display the panel and process the result
//    [panel beginWithCompletionHandler:^(NSModalResponse result) {
//        if (result == NSModalResponseOK) {
//            NSURL *selectedFolder = [[panel URLs] firstObject];
//            NSLog(@"Selected folder: %@", [selectedFolder path]);
//            
//            // Convert the folder path to a C string
//            lpcString_t  folderPath = [[selectedFolder path] UTF8String];
//            
//            // Call the C callback with the selected folder path
//            if (callback) {
//                callback(folderPath);
//            }
//        }
//    }];
//}

bool_t SYS_GetOpenFileName(struct _OPENFILENAME const *ofn) {
  @autoreleasepool {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    NSInteger result = [panel runModal];
    if (result == NSModalResponseOK) {
      NSString *path = [[panel URL] path];
      strncpy(ofn->lpstrFile, [path UTF8String], ofn->nMaxFile);
      return TRUE;
    }
    return FALSE;
  }
}

bool_t SYS_GetFolderName(struct _OPENFILENAME const *ofn) {
  @autoreleasepool {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = NO;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
    NSInteger result = [panel runModal];
    if (result == NSModalResponseOK) {
      NSString *path = [[panel URL] path];
      strncpy(ofn->lpstrFile, [path UTF8String], ofn->nMaxFile);
      return TRUE;
    }
    return FALSE;
  }
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int SYS_ExecuteProcess(lpcString_t process) {
#if 0
    pid_t pid;
    
    // Fork the process
    pid = fork();
    if (pid < 0) {
        // Fork failed
        perror("fork");
        return 1;
    }
    
    if (pid == 0) { // Child process
                    // Define the command and arguments
        LPSTR command = "./orca";
        LPSTR args[] = {
            "./orca",
            "-data=/Users/igor/Developer/ui-framework/projects/cluster/Cluster.xml",
            "-start=Cluster/Screens/InstrumentCluster",
            NULL // NULL-terminate the argument list
        };
        
        // Execute the command
        execvp(command, args);
        
        // If execvp fails
        perror("execvp");
        exit(1);
    } else { // Parent process
             // Do not wait for the child process
        Con_Error("Child process (PID %d) started in parallel", pid);
        
        // Optionally, you can perform other tasks here in the parent process
        
        // Return 0 to indicate success
        return 0;
    }
#endif
	return 0;
}

void SYS_Init(void) {
  //    id menubar     = [[NSMenu new] autorelease];
  //    id appMenuItem = [[NSMenuItem new] autorelease];
  //    [menubar addItem:appMenuItem];
  //
  //    id appMenu      = [[NSMenu new] autorelease];
  //    id appName      = [[NSProcessInfo processInfo] processName];
  //    id quitTitle    = [@"Quit " stringByAppendingString:appName];
  //    id quitMenuItem = [[[NSMenuItem alloc] initWithTitle:quitTitle
  //                                                  action:@selector(terminate:)
  //                                           keyEquivalent:@"q"] autorelease];
  //    [appMenu addItem:quitMenuItem];
  //    [appMenuItem setSubmenu:appMenu];
  
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp finishLaunching];
  //    [NSApp setMainMenu:menubar];
  
  NSAppearance *appearance = [NSApp effectiveAppearance];
  NSAppearanceName bestMatch = [appearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameDarkAqua, NSAppearanceNameAqua]];
  
  if ([bestMatch isEqualToString:NSAppearanceNameDarkAqua]) {
    NSLog(@"App started in Dark Mode");
  } else {
    NSLog(@"App started in Light Mode");
  }
}

void SYS_Shutdown(void) {
}
