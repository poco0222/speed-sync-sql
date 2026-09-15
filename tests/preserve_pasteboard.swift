import AppKit
import Foundation

// Keep every representation in memory; EOF also restores when the test runner exits early.
let board = NSPasteboard.general
let saved = (board.pasteboardItems ?? []).map { item in
    item.types.map { type -> (NSPasteboard.PasteboardType, Data) in
        guard let data = item.data(forType: type) else { exit(1) }
        return (type, data)
    }
}
print("ready")
fflush(stdout)
_ = readLine()
let restored = saved.map { representations in
    let item = NSPasteboardItem()
    for (type, data) in representations { item.setData(data, forType: type) }
    return item
}
board.clearContents()
if !restored.isEmpty && !board.writeObjects(restored) { exit(1) }
