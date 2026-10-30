#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>
#include "WebDialogs.h"

static NSAlert* makeAlert(NSString* msg) {
    NSAlert* a = [[[NSAlert alloc] init] autorelease];
    a.messageText = @"Elixir Desktop";
    a.informativeText = msg ? msg : @"";
    return a;
}
static void alertPanel(id, SEL, WKWebView*, NSString* msg, WKFrameInfo*, void (^done)(void)) {
    NSAlert* a = makeAlert(msg);
    [a addButtonWithTitle:@"OK"];
    [a runModal];
    done();
}
static void confirmPanel(id, SEL, WKWebView*, NSString* msg, WKFrameInfo*, void (^done)(BOOL)) {
    NSAlert* a = makeAlert(msg);
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    done([a runModal] == NSAlertFirstButtonReturn);
}
static void promptPanel(id, SEL, WKWebView*, NSString* msg, NSString* dflt, WKFrameInfo*, void (^done)(NSString*)) {
    NSAlert* a = makeAlert(msg);
    [a addButtonWithTitle:@"OK"];
    [a addButtonWithTitle:@"Cancel"];
    NSTextField* field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)] autorelease];
    field.stringValue = dflt ? dflt : @"";
    a.accessoryView = field;
    [a.window setInitialFirstResponder:field];
    done([a runModal] == NSAlertFirstButtonReturn ? field.stringValue : nil);
}

static WKWebView* findWebView(NSView* v) {
    if ([v isKindOfClass:[WKWebView class]]) return (WKWebView*) v;
    for (NSView* s in v.subviews) if (WKWebView* w = findWebView(s)) return w;
    return nil;
}

bool addWebDialogs(void* nsView) {
    WKWebView* web = findWebView((NSView*) nsView);
    if (web == nil || web.UIDelegate == nil) return false;
    Class c = object_getClass(web.UIDelegate);
    class_addMethod(c, @selector(webView:runJavaScriptAlertPanelWithMessage:initiatedByFrame:completionHandler:), (IMP) alertPanel, "v@:@@@@?");
    class_addMethod(c, @selector(webView:runJavaScriptConfirmPanelWithMessage:initiatedByFrame:completionHandler:), (IMP) confirmPanel, "v@:@@@@?");
    class_addMethod(c, @selector(webView:runJavaScriptTextInputPanelWithPrompt:defaultText:initiatedByFrame:completionHandler:), (IMP) promptPanel, "v@:@@@@@?");
    return true;
}
