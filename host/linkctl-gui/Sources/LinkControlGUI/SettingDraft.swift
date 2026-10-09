import LinkControlCore

/// An explicit-Apply edit survives background updates to other camera settings.
struct SettingDraft {
    private(set) var value: Int
    private(set) var isEdited = false

    init(control: CameraSetting) {
        value = control.value ?? control.defaultValue ?? control.min
    }

    mutating func edit(_ value: Int) {
        self.value = value
        isEdited = true
    }

    mutating func submitted() { isEdited = false }

    mutating func reconcile(_ control: CameraSetting) {
        if !isEdited || !control.accepts(value) {
            value = control.value ?? control.defaultValue ?? control.min
            isEdited = false
        }
    }
}
