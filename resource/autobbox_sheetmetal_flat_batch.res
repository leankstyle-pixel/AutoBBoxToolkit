(Dialog autobbox_sheetmetal_flat_batch
    (Components
        (SubLayout                      SheetmetalFlatBatchPage)
    )
    (Resources
        (.Label                         "Sheetmetal Flat Batch")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                SheetmetalFlatBatchPage
            )
        )
    )
)

(Layout SheetmetalFlatBatchPage
    (Components
        (Label                          SummaryLabel)
        (Table                          TargetTable)
        (SubLayout                      ActionBar)
    )
    (Resources
        (SummaryLabel.Label             "Sheetmetal flat batch")
        (SummaryLabel.TopOffset         4)
        (SummaryLabel.BottomOffset      2)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (SummaryLabel.AttachLeft        True)
        (SummaryLabel.AttachRight       True)
        (TargetTable.Columns            160)
        (TargetTable.MinRows            8)
        (TargetTable.VisibleRows        14)
        (TargetTable.RowNames           "ROW_0")
        (TargetTable.ColumnNames        "USE"
                                        "MODEL"
                                        "PATH"
                                        "SIMPREP"
                                        "FAMILY"
                                        "STATUS")
        (TargetTable.RowLabels          "")
        (TargetTable.ColumnLabels       "Select"
                                        "Model"
                                        "Path"
                                        "Flat Simprep"
                                        "Flat Family/State"
                                        "Status")
        (TargetTable.ColumnWidths       8
                                        24
                                        26
                                        24
                                        28
                                        52)
        (TargetTable.ShowGrid           True)
        (TargetTable.TopOffset          4)
        (TargetTable.BottomOffset       4)
        (TargetTable.LeftOffset         8)
        (TargetTable.RightOffset        8)
        (TargetTable.AttachLeft         True)
        (TargetTable.AttachRight        True)
        (TargetTable.AttachTop          True)
        (TargetTable.AttachBottom       True)
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
                TargetTable
                ActionBar
            )
        )
    )
)

(Layout ActionBar
    (Components
        (PushButton                     SelectAllBtn)
        (PushButton                     ClearBtn)
        (PushButton                     RefreshBtn)
        (PushButton                     CreateSimprepBtn)
        (PushButton                     CreateFamilyFlatBtn)
        (PushButton                     DeleteBtn)
        (PushButton                     CloseBtn)
    )
    (Resources
        (SelectAllBtn.Label             "Select All")
        (ClearBtn.Label                 "Clear")
        (RefreshBtn.Label               "Refresh")
        (CreateSimprepBtn.Label         "Create Simprep")
        (CreateFamilyFlatBtn.Label      "Create Family Flat")
        (DeleteBtn.Label                "Delete Selected")
        (CloseBtn.Label                 "Close")
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 0 0 0 1 0)
                (Pos 1 1)
                SelectAllBtn
                (Pos 1 2)
                ClearBtn
                (Pos 1 3)
                RefreshBtn
                (Pos 1 4)
                CreateSimprepBtn
                (Pos 1 5)
                CreateFamilyFlatBtn
                (Pos 1 6)
                DeleteBtn
                (Pos 1 8)
                CloseBtn
            )
        )
    )
)

(TableLayout TargetTable
    (Components
        (CheckButton                    BaseTargetCheck)
    )
    (Resources
        (BaseTargetCheck.Label          "")
    )
)
