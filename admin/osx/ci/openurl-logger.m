// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Fake browser for the NoDrive macOS smoke test (tools/screenshots/macos.sh). Test tool only,
// never shipped. Built on the CI runner:
//   clang -dynamiclib -fobjc-arc -framework AppKit -framework CoreServices \
//         -o openurl-logger.dylib admin/osx/ci/openurl-logger.m
// and loaded into the app with DYLD_INSERT_LIBRARIES (possible because the CI build is ad-hoc
// signed without the hardened runtime).
//
// Qt opens links on macOS through NSWorkspace. This library replaces the NSWorkspace "open URL"
// methods (and LSOpenCFURLRef) so that every URL the app hands to the browser is printed to
// stderr as "NODRIVE_OPEN_URL <method> <url>" and no browser is started.

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>
#import <objc/runtime.h>
#include <stdio.h>

static void nodriveLogURL(NSURL *url, const char *via)
{
    const char *text = url.absoluteString.UTF8String;
    fprintf(stderr, "NODRIVE_OPEN_URL %s %s\n", via, text ? text : "(null)");
    fflush(stderr);
}

static BOOL nodriveOpenURL(id self, SEL _cmd, NSURL *url)
{
    (void)self;
    (void)_cmd;
    nodriveLogURL(url, "openURL:");
    return YES;
}

static void nodriveOpenURLConfiguration(id self, SEL _cmd, NSURL *url, id configuration,
                                        void (^completionHandler)(NSRunningApplication *, NSError *))
{
    (void)self;
    (void)_cmd;
    (void)configuration;
    nodriveLogURL(url, "openURL:configuration:completionHandler:");
    if (completionHandler) {
        completionHandler(nil, nil);
    }
}

static OSStatus nodriveLSOpenCFURLRef(CFURLRef url, CFURLRef *launchedURL)
{
    nodriveLogURL((__bridge NSURL *)url, "LSOpenCFURLRef");
    if (launchedURL) {
        *launchedURL = NULL;
    }
    return noErr;
}

// dyld interposing: calls to LSOpenCFURLRef from any image go to our function instead.
__attribute__((used)) static struct {
    const void *replacement;
    const void *replacee;
} nodriveInterposers[] __attribute__((section("__DATA,__interpose"))) = {
    {(const void *)nodriveLSOpenCFURLRef, (const void *)LSOpenCFURLRef},
};

__attribute__((constructor)) static void nodriveInstallOpenURLLogger(void)
{
    Class workspace = [NSWorkspace class];
    Method m = class_getInstanceMethod(workspace, @selector(openURL:));
    if (m) {
        method_setImplementation(m, (IMP)nodriveOpenURL);
    }
    m = class_getInstanceMethod(workspace, @selector(openURL:configuration:completionHandler:));
    if (m) {
        method_setImplementation(m, (IMP)nodriveOpenURLConfiguration);
    }
    fprintf(stderr, "NODRIVE_OPEN_URL_LOGGER installed in pid %d\n", getpid());
    fflush(stderr);
}
