.pragma library
.import "qrcode-generator.js" as Vendor

function matrix(payload) {
    if (!payload || payload.length === 0) {
        return { size: 0, modules: [] }
    }

    var qr = Vendor.qrcode(0, "M")
    qr.addData(payload)
    qr.make()

    var size = qr.getModuleCount()
    var modules = []
    for (var row = 0; row < size; row++) {
        var line = []
        for (var column = 0; column < size; column++) {
            line.push(qr.isDark(row, column))
        }
        modules.push(line)
    }

    return { size: size, modules: modules }
}
