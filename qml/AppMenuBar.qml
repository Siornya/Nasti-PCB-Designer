import QtQuick
import QtQuick.Controls

MenuBar {
	id: menuBar
	required property ApplicationWindow editorWindow
	required property QtObject projectController
	required property ProjectFlow documentFlow
	required property bool interactionEnabled
	readonly property var translations: ({
		zh: {
			file: "文件",
			openProject: "打开工程…",
			saveProject: "保存工程",
			saveProjectAs: "另存为工程…",
			exportMenu: "导出",
			binaryImage: "黑白图片…",
			grayscaleImage: "灰度图片…",
			lithophane: "透光浮雕（STL）…",
			edit: "编辑",
			undo: "撤销",
			redo: "重做"
		},
		en: {
			file: "File",
			openProject: "Open Project…",
			saveProject: "Save Project",
			saveProjectAs: "Save Project As…",
			exportMenu: "Export",
			binaryImage: "Black and White Image…",
			grayscaleImage: "Grayscale Image…",
			lithophane: "Lithophane (STL)…",
			edit: "Edit",
			undo: "Undo",
			redo: "Redo"
		},
		ja: {
			file: "ファイル",
			openProject: "プロジェクトを開く…",
			saveProject: "プロジェクトを保存",
			saveProjectAs: "名前を付けて保存…",
			exportMenu: "エクスポート",
			binaryImage: "白黒画像…",
			grayscaleImage: "グレースケール画像…",
			lithophane: "リトファン（STL）…",
			edit: "編集",
			undo: "元に戻す",
			redo: "やり直す"
		}
	})
	readonly property var labels: {
		for (const tag of locale.uiLanguages) {
			const language = tag.split("-")[0].toLowerCase()
			if (translations[language]) return translations[language]
		}
		return translations.en
	}

	Menu {
		title: menuBar.labels.file
		enabled: !menuBar.projectController.busy && menuBar.interactionEnabled
		Action { text: menuBar.labels.openProject; shortcut: StandardKey.Open; onTriggered: menuBar.documentFlow.openProject() }
		Action {
			text: menuBar.labels.saveProject
			shortcut: StandardKey.Save
			onTriggered: menuBar.documentFlow.saveProject()
		}
		Action { text: menuBar.labels.saveProjectAs; shortcut: StandardKey.SaveAs; onTriggered: menuBar.documentFlow.saveProjectAs() }
	}
	Menu {
		title: menuBar.labels.exportMenu
		enabled: menuBar.interactionEnabled && !menuBar.projectController.busy
			&& menuBar.projectController.resultUrl.length > 0
		Action { text: menuBar.labels.binaryImage; onTriggered: menuBar.editorWindow.exportImage(false) }
		Action { text: menuBar.labels.lithophane; onTriggered: menuBar.editorWindow.openLithophane() }
		Action { text: menuBar.labels.grayscaleImage; onTriggered: menuBar.editorWindow.exportImage(true) }
	}
	Menu {
		title: menuBar.labels.edit
		Action {
			text: menuBar.labels.undo
			shortcut: StandardKey.Undo
			enabled: menuBar.projectController.canUndo && menuBar.interactionEnabled
			onTriggered: menuBar.editorWindow.navigateHistory(false)
		}
		Action {
			text: menuBar.labels.redo
			shortcut: StandardKey.Redo
			enabled: menuBar.projectController.canRedo && menuBar.interactionEnabled
			onTriggered: menuBar.editorWindow.navigateHistory(true)
		}
	}
}
