import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

MenuBar {
	id: menuBar
	required property ApplicationWindow editorWindow
	required property QtObject projectController
	required property FileDialog openDialog
	required property FileDialog saveDialog
	required property bool interactionEnabled

	Menu {
		title: "文件"
		enabled: !menuBar.projectController.busy && menuBar.interactionEnabled
		Action { text: "打开工程…"; shortcut: StandardKey.Open; onTriggered: menuBar.openDialog.open() }
		Action {
			text: "保存工程"
			shortcut: StandardKey.Save
			onTriggered: {
				if (menuBar.editorWindow.projectFile.toString().length)
					menuBar.projectController.saveProject(menuBar.editorWindow.projectFile)
				else menuBar.saveDialog.open()
			}
		}
		Action { text: "另存为工程…"; shortcut: StandardKey.SaveAs; onTriggered: menuBar.saveDialog.open() }
	}
	Menu {
		title: "编辑"
		Action {
			text: "撤销"
			shortcut: StandardKey.Undo
			enabled: menuBar.projectController.canUndo && menuBar.interactionEnabled
			onTriggered: menuBar.editorWindow.navigateHistory(false)
		}
		Action {
			text: "重做"
			shortcut: StandardKey.Redo
			enabled: menuBar.projectController.canRedo && menuBar.interactionEnabled
			onTriggered: menuBar.editorWindow.navigateHistory(true)
		}
	}
}
