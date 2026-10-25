// SPDX-FileCopyrightText: 2026 Nourax
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NoDrive macOS CI helper for tools/screenshots/macos.sh. Test tool only, never shipped.
// Built on the CI runner:  swiftc -O -o nodrive-ui admin/osx/ci/nodrive-ui.swift
//
// All coordinates are global display points with the origin at the top left of the main
// display (the CGWindowList / CGEvent convention).
//
//   nodrive-ui trust                  Accessibility, screen recording and event posting access
//   nodrive-ui screen                 main display: "<points w> <points h> <pixels w> <pixels h>"
//   nodrive-ui windows PID            on-screen windows of PID, one per line:
//                                     "<id> <layer> <x> <y> <w> <h> <alpha> <name>"
//   nodrive-ui activate PID           bring the app to the front
//   nodrive-ui click X Y [right]      mouse click
//   nodrive-ui move X Y               move the mouse
//   nodrive-ui key escape|return|tab  press one key
//   nodrive-ui statusitem PID         frame of the app's menu bar item: "<x> <y> <w> <h> <source>"
//   nodrive-ui ax PID                 accessibility tree of the app (role, title, frame), for logs

import AppKit
import ApplicationServices
import CoreGraphics
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

func pidArg(_ args: [String], _ i: Int) -> pid_t {
    guard args.count > i, let p = Int32(args[i]) else { fail("missing PID") }
    return p
}

func pointArg(_ args: [String], _ i: Int) -> CGPoint {
    guard args.count > i + 1, let x = Double(args[i]), let y = Double(args[i + 1]) else { fail("missing X Y") }
    return CGPoint(x: x, y: y)
}

func windowList() -> [[String: Any]] {
    let info = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID)
    return (info as? [[String: Any]]) ?? []
}

func bounds(_ w: [String: Any]) -> CGRect {
    guard let b = w[kCGWindowBounds as String] as? NSDictionary,
          let r = CGRect(dictionaryRepresentation: b) else { return .zero }
    return r
}

func fmt(_ v: CGFloat) -> String { String(Int(v.rounded())) }

func post(_ type: CGEventType, _ p: CGPoint, _ button: CGMouseButton) {
    let source = CGEventSource(stateID: .hidSystemState)
    guard let e = CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: p, mouseButton: button) else {
        fail("cannot create mouse event")
    }
    e.post(tap: .cghidEventTap)
}

func axValue<T>(_ element: AXUIElement, _ attribute: String) -> T? {
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success else { return nil }
    return value as? T
}

func axFrame(_ element: AXUIElement) -> CGRect? {
    var pos: CFTypeRef?
    var size: CFTypeRef?
    guard AXUIElementCopyAttributeValue(element, kAXPositionAttribute as CFString, &pos) == .success,
          AXUIElementCopyAttributeValue(element, kAXSizeAttribute as CFString, &size) == .success,
          let posValue = pos, let sizeValue = size,
          CFGetTypeID(posValue) == AXValueGetTypeID(), CFGetTypeID(sizeValue) == AXValueGetTypeID() else { return nil }
    var p = CGPoint.zero
    var s = CGSize.zero
    AXValueGetValue(posValue as! AXValue, .cgPoint, &p)
    AXValueGetValue(sizeValue as! AXValue, .cgSize, &s)
    return CGRect(origin: p, size: s)
}

func axDump(_ element: AXUIElement, _ depth: Int, _ budget: inout Int) {
    guard budget > 0, depth < 14 else { return }
    budget -= 1
    let role: String = axValue(element, kAXRoleAttribute) ?? "?"
    let subrole: String = axValue(element, kAXSubroleAttribute) ?? ""
    let title: String = axValue(element, kAXTitleAttribute) ?? ""
    let desc: String = axValue(element, kAXDescriptionAttribute) ?? ""
    let value: String = axValue(element, kAXValueAttribute) ?? ""
    var line = String(repeating: "  ", count: depth) + role
    if !subrole.isEmpty { line += "/" + subrole }
    for (k, v) in [("title", title), ("desc", desc), ("value", value)] where !v.isEmpty {
        line += " \(k)=\"\(v.prefix(80))\""
    }
    if let f = axFrame(element) {
        line += " frame=\(fmt(f.minX)),\(fmt(f.minY)) \(fmt(f.width))x\(fmt(f.height))"
    }
    print(line)
    let children: [AXUIElement] = axValue(element, kAXChildrenAttribute) ?? []
    for child in children { axDump(child, depth + 1, &budget) }
}

let args = CommandLine.arguments
guard args.count >= 2 else { fail("usage: nodrive-ui trust|screen|windows|activate|click|move|key|statusitem|ax ...") }

switch args[1] {
case "trust":
    print("accessibility \(AXIsProcessTrusted())")
    print("screen-capture \(CGPreflightScreenCaptureAccess())")
    print("post-event \(CGPreflightPostEventAccess())")

case "screen":
    let id = CGMainDisplayID()
    let b = CGDisplayBounds(id)
    print("\(fmt(b.width)) \(fmt(b.height)) \(CGDisplayPixelsWide(id)) \(CGDisplayPixelsHigh(id))")
    if let mode = CGDisplayCopyDisplayMode(id) {
        print("mode \(mode.width)x\(mode.height) pixels \(mode.pixelWidth)x\(mode.pixelHeight)")
    }

case "windows":
    let pid = pidArg(args, 2)
    for w in windowList() {
        guard (w[kCGWindowOwnerPID as String] as? Int32) == pid else { continue }
        let r = bounds(w)
        let id = w[kCGWindowNumber as String] as? Int ?? 0
        let layer = w[kCGWindowLayer as String] as? Int ?? 0
        let alpha = w[kCGWindowAlpha as String] as? Double ?? 1
        let name = (w[kCGWindowName as String] as? String ?? "").replacingOccurrences(of: "\n", with: " ")
        print("\(id) \(layer) \(fmt(r.minX)) \(fmt(r.minY)) \(fmt(r.width)) \(fmt(r.height)) \(alpha) \(name)")
    }

case "activate":
    let pid = pidArg(args, 2)
    guard let app = NSRunningApplication(processIdentifier: pid) else { fail("no running application \(pid)") }
    print("activate \(app.activate(options: [.activateAllWindows]))")

case "click":
    let p = pointArg(args, 2)
    let right = args.count > 4 && args[4] == "right"
    let (down, up, button): (CGEventType, CGEventType, CGMouseButton) =
        right ? (.rightMouseDown, .rightMouseUp, .right) : (.leftMouseDown, .leftMouseUp, .left)
    post(.mouseMoved, p, button)
    usleep(250_000)
    post(down, p, button)
    usleep(120_000)
    post(up, p, button)
    usleep(250_000)

case "move":
    post(.mouseMoved, pointArg(args, 2), .left)

case "key":
    guard args.count > 2 else { fail("missing key") }
    let codes: [String: CGKeyCode] = ["escape": 53, "return": 36, "tab": 48]
    guard let code = codes[args[2]] else { fail("unknown key \(args[2])") }
    let source = CGEventSource(stateID: .hidSystemState)
    CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: true)?.post(tap: .cghidEventTap)
    usleep(80_000)
    CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: false)?.post(tap: .cghidEventTap)
    usleep(200_000)

case "statusitem":
    let pid = pidArg(args, 2)
    let app = AXUIElementCreateApplication(pid)
    if let extras: AXUIElement = axValue(app, "AXExtrasMenuBar") {
        let items: [AXUIElement] = axValue(extras, kAXChildrenAttribute) ?? []
        for item in items {
            if let f = axFrame(item), f.width > 0 {
                print("\(fmt(f.minX)) \(fmt(f.minY)) \(fmt(f.width)) \(fmt(f.height)) accessibility")
                exit(0)
            }
        }
    }
    // Fallback: the status item's own window (menu bar level) in the window list.
    for w in windowList() {
        guard (w[kCGWindowOwnerPID as String] as? Int32) == pid else { continue }
        let layer = w[kCGWindowLayer as String] as? Int ?? 0
        let r = bounds(w)
        if layer >= Int(CGWindowLevelForKey(.statusWindow)) && r.minY < 40 && r.width < 200 {
            print("\(fmt(r.minX)) \(fmt(r.minY)) \(fmt(r.width)) \(fmt(r.height)) window-layer-\(layer)")
            exit(0)
        }
    }
    fail("no menu bar item found for \(pid)")

case "ax":
    var budget = 400
    axDump(AXUIElementCreateApplication(pidArg(args, 2)), 0, &budget)

default:
    fail("unknown command \(args[1])")
}
