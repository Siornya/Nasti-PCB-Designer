import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
	id: flow
	objectName: "projectFlow"
	required property ApplicationWindow editorWindow
	required property QtObject projectController
	required property bool pendingPreview
	property url projectFile
	signal projectOpened(url file)

	function openProject() { openProjectDialog.open() }
	function saveProjectAs() { saveProjectDialog.open() }
	function saveProject() {
		if (projectFile.toString().length) saveBeforeContinuing(projectFile)
		else saveProjectAs()
	}
	property bool closeRequested: false
	property bool closeApproved: false
	property url nextProject

	function finishDocumentAction() {
		if (closeRequested) {
			closeRequested = false
			closeApproved = true
			editorWindow.close()
			closeApproved = false
		} else if (nextProject.toString().length) {
			const file = nextProject
			nextProject = ""
			if (projectController.openProject(file)) {
				projectFile = file
				projectOpened(file)
			}
		}
	}
	function cancelDocumentAction() {
		closeRequested = false
		nextProject = ""
	}
	function checkDocumentAction() {
		if (!closeRequested && !nextProject.toString().length) return
		if (projectController.busy || pendingPreview || unsavedDialog.visible || saveProjectDialog.visible) return
		if (projectController.modified) unsavedDialog.open()
		else finishDocumentAction()
	}
	function saveBeforeContinuing(file) {
		if (projectController.saveProject(file)) {
			projectFile = file
			finishDocumentAction()
		} else cancelDocumentAction()
	}
	function handleClosing(event) {
		if (closeApproved || (!projectController.modified && !pendingPreview && !projectController.busy)) return
		event.accepted = false
		closeRequested = true
		Qt.callLater(checkDocumentAction)
	}
	Connections {
		target: flow.projectController
		function onChanged() {
			if (flow.closeRequested) Qt.callLater(flow.checkDocumentAction)
		}
	}
	Dialog {
		id: unsavedDialog
		objectName: "unsavedDialog"
		title: "工程尚未保存"
		modal: true
		parent: flow.editorWindow.contentItem
		anchors.centerIn: parent
		closePolicy: Popup.CloseOnEscape
		onRejected: flow.cancelDocumentAction()
		Label { text: "是否保存对当前工程的修改？" }
		footer: RowLayout {
			Button { text: "取消"; onClicked: unsavedDialog.reject() }
			Button {
				text: "放弃修改"
				onClicked: {
					unsavedDialog.close()
					flow.finishDocumentAction()
				}
			}
			Button {
				text: "保存"
				highlighted: true
				onClicked: {
					unsavedDialog.close()
					if (flow.projectFile.toString().length) flow.saveBeforeContinuing(flow.projectFile)
					else saveProjectDialog.open()
				}
			}
		}
	}
	FileDialog {
		id: openProjectDialog
		title: "打开工程"
		nameFilters: ["NastiDesigner 工程 (*.nasti)"]
		onAccepted: {
			flow.nextProject = selectedFile
			flow.checkDocumentAction()
		}
	}
	FileDialog {
		id: saveProjectDialog
		title: "保存工程"
		fileMode: FileDialog.SaveFile
		nameFilters: ["NastiDesigner 工程 (*.nasti)"]
		defaultSuffix: "nasti"
		onAccepted: flow.saveBeforeContinuing(selectedFile)
		onRejected: flow.cancelDocumentAction()
	}
}
