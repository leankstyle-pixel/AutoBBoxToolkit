(Dialog autobbox_batch_rename
    (Components
        (SubLayout                      BatchRenamePage)
    )
    (Resources
        (.Label                         "Batch Rename")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                BatchRenamePage
            )
        )
    )
)

(Layout BatchRenamePage
    (Components
        (Label                          SummaryLabel)
        (Table                          RenameTable)
        (SubLayout                      ActionBar)
    )
    (Resources
        (SummaryLabel.Label             "Batch rename")
        (SummaryLabel.TopOffset         4)
        (SummaryLabel.BottomOffset      2)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (RenameTable.Columns            96)
        (RenameTable.MinRows            8)
        (RenameTable.VisibleRows        14)
        (RenameTable.RowNames           "ROW_0")
        (RenameTable.ColumnNames        "SELECT"
                                        "MODEL"
                                        "NEWNAME"
                                        "COMMON"
                                        "STATUS")
        (RenameTable.RowLabels          "")
        (RenameTable.ColumnLabels       "Select"
                                        "Model"
                                        "New Model Name"
                                        "PTC_COMMON_NAME"
                                        "Status")
        (RenameTable.ColumnWidths       10
                                        24
                                        28
                                        30
                                        28)
        (RenameTable.ShowGrid           True)
        (RenameTable.TopOffset          4)
        (RenameTable.BottomOffset       4)
        (RenameTable.LeftOffset         8)
        (RenameTable.RightOffset        8)
        (RenameTable.AttachLeft         True)
        (RenameTable.AttachRight        True)
        (RenameTable.AttachTop          True)
        (RenameTable.AttachBottom       True)
        (ActionBar.AttachLeft           True)
        (ActionBar.AttachRight          True)
        (ActionBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                SummaryLabel
                RenameTable
                ActionBar
            )
        )
    )
)

(Layout ActionBar
    (Components
        (PushButton                     PasteBtn)
        (PushButton                     ClearBtn)
        (PushButton                     ReplaceBtn)
        (PushButton                     SequenceBtn)
        (PushButton                     ValidateBtn)
        (PushButton                     ResetBtn)
        (PushButton                     RefreshBtn)
        (PushButton                     ApplyBtn)
        (PushButton                     CloseBtn)
    )
    (Resources
        (PasteBtn.Label                 "Paste")
        (ClearBtn.Label                 "Clear")
        (ReplaceBtn.Label               "Replace")
        (SequenceBtn.Label              "Sequence")
        (ValidateBtn.Label              "Validate")
        (ResetBtn.Label                 "Reset")
        (RefreshBtn.Label               "Refresh")
        (ApplyBtn.Label                 "Apply")
        (CloseBtn.Label                 "Close")
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 0 0 0 0 0 0)
                PasteBtn
                ClearBtn
                ReplaceBtn
                SequenceBtn
                ValidateBtn
                ResetBtn
                RefreshBtn
                ApplyBtn
                CloseBtn
            )
        )
    )
)

(TableLayout RenameTable
    (Components
        (CheckButton                    SelectBase)
        (InputPanel                     CellInputBase)
    )
    (Resources
        (SelectBase.Label               "")
        (CellInputBase.Columns          28)
        (CellInputBase.MaxLen           80)
    )
)
