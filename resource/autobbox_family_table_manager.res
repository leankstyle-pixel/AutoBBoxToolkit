(Dialog autobbox_family_table_manager
    (Components
        (SubLayout MainPage)
    )
    (Resources
        (.Label "Multi-Level Family Table Excel Manager")
        (.Resizeable True)
        (.MaximizeButton True)
        (.MinimizeButton True)
        (.Layout (Grid (Rows 1) (Cols 1) MainPage))
    )
)

(Layout MainPage
    (Components
        (Label StatusLabel)
        (SubLayout ToolbarPanel)
        (InputPanel QuickInput)
        (SubLayout BodyPanel)
        (SubLayout HiddenPanel)
    )
    (Resources
        (StatusLabel.Label "Ready. Export all family-table levels to Excel, import workbook for preview, then apply to Creo after confirmation.")
        (StatusLabel.AttachLeft True)
        (StatusLabel.AttachRight True)
        (StatusLabel.LeftOffset 6)
        (StatusLabel.RightOffset 6)
        (QuickInput.Columns 120)
        (QuickInput.MaxLen 260)
        (QuickInput.SelectionVisible True)
        (QuickInput.AttachLeft True)
        (QuickInput.AttachRight True)
        (ToolbarPanel.AttachLeft True)
        (ToolbarPanel.AttachRight True)
        (BodyPanel.AttachLeft True)
        (BodyPanel.AttachRight True)
        (BodyPanel.AttachTop True)
        (BodyPanel.AttachBottom True)
        (HiddenPanel.Visible False)
        (.AttachLeft True)
        (.AttachRight True)
        (.AttachTop True)
        (.AttachBottom True)
        (.Layout
            (Grid (Rows 0 0 0 1 0) (Cols 1)
                StatusLabel
                ToolbarPanel
                QuickInput
                BodyPanel
                HiddenPanel
            )
        )
    )
)

(Layout BodyPanel
    (Components
        (List LevelList)
        (DrawingArea LevelSplitter)
        (Table FtGrid)
    )
    (Resources
        (LevelList.VisibleRows 18)
        (LevelList.MinRows 10)
        (LevelList.Columns 1)
        (LevelList.LeftOffset 6)
        (LevelList.RightOffset 0)
        (LevelList.TopOffset 6)
        (LevelList.BottomOffset 6)
        (LevelList.AttachLeft True)
        (LevelList.AttachRight True)
        (LevelList.AttachTop True)
        (LevelList.AttachBottom True)
        (LevelSplitter.Decorated False)
        (LevelSplitter.DrawingWidth 2)
        (LevelSplitter.DrawingMinWidth 2)
        (LevelSplitter.AttachTop True)
        (LevelSplitter.AttachBottom True)
        (LevelSplitter.Cursor "UI h resize cursor image")
        (FtGrid.TopOffset 6)
        (FtGrid.RightOffset 6)
        (FtGrid.BottomOffset 6)
        (FtGrid.SelectionPolicy 4)
        (FtGrid.Columns 120)
        (FtGrid.VisibleRows 22)
        (FtGrid.TruncateLabel False)
        (FtGrid.ShowGrid True)
        (FtGrid.LeftOffset 0)
        (FtGrid.AttachLeft True)
        (FtGrid.AttachRight True)
        (FtGrid.AttachTop True)
        (FtGrid.AttachBottom True)
        (.AttachLeft True)
        (.AttachRight True)
        (.AttachTop True)
        (.AttachBottom True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 1)
                LevelList
                LevelSplitter
                FtGrid
            )
        )
    )
)

(Layout ToolbarPanel
    (Components
        (PushButton FileRefresh)
        (PushButton EditDeleteLevel)
        (PushButton EditCloneInstance)
        (PushButton EditCloneTree)
        (PushButton FileExportAll)
        (PushButton FileImport)
        (PushButton FileApply)
        (PushButton ViewLog)
        (PushButton FileClose)
    )
    (Resources
        (FileRefresh.Label "刷新")
        (FileRefresh.HelpText "重新读取当前模型的多层族表")
        (EditDeleteLevel.Label "删除层级")
        (EditDeleteLevel.HelpText "删除当前层树选中的实例层级")
        (EditCloneInstance.Label "快速复制实例")
        (EditCloneInstance.HelpText "复制当前 level 的选中实例；支持在输入框填复制数量")
        (EditCloneTree.Label "高级复制子树")
        (EditCloneTree.HelpText "复制当前实例及已发现的子层级；复制后建议立即转官方族表验证")
        (FileExportAll.Label "导出全部层Excel")
        (FileExportAll.HelpText "将当前 generic 开始的全部多层族表导出为多 sheet 工作簿")
        (FileImport.Label "从Excel导入预览")
        (FileImport.HelpText "读取工作簿到弹窗内存态，先预览差异，不直接写 Creo")
        (FileApply.Label "应用更新到Creo")
        (FileApply.HelpText "用户确认后将导入/编辑结果写回 Creo")
        (ViewLog.Label "日志")
        (ViewLog.HelpText "查看导出/导入/写回日志")
        (FileClose.Label "关闭")
        (FileClose.HelpText "关闭窗口")
        (.AttachLeft True)
        (.AttachRight True)
        (.Layout
            (Grid (Rows 0) (Cols 0 0 0 0 0 0 0 0 0)
                FileRefresh
                EditDeleteLevel
                EditCloneInstance
                EditCloneTree
                FileExportAll
                FileImport
                FileApply
                ViewLog
                FileClose
            )
        )
    )
)

(Layout HiddenPanel
    (Components
        (PushButton FileExportCurrent)
        (PushButton FileEditOutside)
        (PushButton FileEditExcel)
        (PushButton EditAddColumn)
        (PushButton EditDeleteColumn)
        (PushButton EditMoveLeft)
        (PushButton EditMoveRight)
        (PushButton EditHideColumn)
        (PushButton EditShowAllColumns)
        (PushButton EditAddRow)
        (PushButton EditDeleteRow)
        (PushButton EditLock)
        (PushButton EditUnlock)
        (PushButton EditComment)
        (PushButton ViewSearch)
        (PushButton ViewOpenInstance)
        (PushButton ViewPreviewInstance)
        (PushButton FormatNarrow)
        (PushButton FormatWider)
        (PushButton FormatReset)
        (PushButton ToolsValidate)
        (PushButton ToolsNative)
        (PushButton ToolsEnhanced)
        (PushButton HelpAbout)
    )
    (Resources
        (FileExportCurrent.Visible False)
        (FileEditOutside.Visible False)
        (FileEditExcel.Visible False)
        (EditAddColumn.Visible False)
        (EditDeleteColumn.Visible False)
        (EditMoveLeft.Visible False)
        (EditMoveRight.Visible False)
        (EditHideColumn.Visible False)
        (EditShowAllColumns.Visible False)
        (EditAddRow.Visible False)
        (EditDeleteRow.Visible False)
        (EditLock.Visible False)
        (EditUnlock.Visible False)
        (EditComment.Visible False)
        (ViewSearch.Visible False)
        (ViewOpenInstance.Visible False)
        (ViewPreviewInstance.Visible False)
        (FormatNarrow.Visible False)
        (FormatWider.Visible False)
        (FormatReset.Visible False)
        (ToolsValidate.Visible False)
        (ToolsNative.Visible False)
        (ToolsEnhanced.Visible False)
        (HelpAbout.Visible False)
        (.Visible False)
        (.Layout
            (Grid (Rows 0 0 0 0) (Cols 0 0 0 0 0 0)
                FileExportCurrent
                FileEditOutside
                FileEditExcel
                EditAddColumn
                EditDeleteColumn
                EditMoveLeft
                EditMoveRight
                EditHideColumn
                EditShowAllColumns
                EditAddRow
                EditDeleteRow
                EditLock
                EditUnlock
                EditComment
                ViewSearch
                ViewOpenInstance
                ViewPreviewInstance
                FormatNarrow
                FormatWider
                FormatReset
                ToolsValidate
                ToolsNative
                ToolsEnhanced
                HelpAbout
            )
        )
    )
)

(TableLayout FtGrid
    (Components
        (InputPanel CellInputBase)
    )
    (Resources
        (CellInputBase.MaxLen 256)
        (CellInputBase.Columns 20)
        (CellInputBase.SelectionVisible True)
    )
)
