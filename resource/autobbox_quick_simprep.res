(Dialog autobbox_quick_simprep
    (Components
        (SubLayout                      QuickSimprepPage)
    )
    (Resources
        (.Label                         "Quick Simplified Rep")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                QuickSimprepPage
            )
        )
    )
)

(Layout QuickSimprepPage
    (Components
        (Label                          SummaryLabel)
        (SubLayout                      BodyPanel)
        (SubLayout                      ActionBar)
    )
    (Resources
        (SummaryLabel.Label             "Quick simplified representation")
        (SummaryLabel.TopOffset         4)
        (SummaryLabel.BottomOffset      2)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (SummaryLabel.AttachLeft        True)
        (SummaryLabel.AttachRight       True)
        (BodyPanel.TopOffset            4)
        (BodyPanel.BottomOffset         4)
        (BodyPanel.LeftOffset           8)
        (BodyPanel.RightOffset          8)
        (BodyPanel.AttachLeft           True)
        (BodyPanel.AttachRight          True)
        (BodyPanel.AttachTop            True)
        (BodyPanel.AttachBottom         True)
        (ActionBar.AttachLeft           True)
        (ActionBar.AttachRight          True)
        (ActionBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                (Pos 1 1)
                SummaryLabel
                (Pos 2 1)
                BodyPanel
                (Pos 3 1)
                ActionBar
            )
        )
    )
)

(Layout BodyPanel
    (Components
        (SubLayout                      LeftRepPanel)
        (SubLayout                      RightCategoryPanel)
    )
    (Resources
        (LeftRepPanel.RightOffset       6)
        (LeftRepPanel.AttachLeft        True)
        (LeftRepPanel.AttachTop         True)
        (LeftRepPanel.AttachBottom      True)
        (RightCategoryPanel.LeftOffset  6)
        (RightCategoryPanel.AttachLeft  True)
        (RightCategoryPanel.AttachRight True)
        (RightCategoryPanel.AttachTop   True)
        (RightCategoryPanel.AttachBottom True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 1)
                (Pos 1 1)
                LeftRepPanel
                (Pos 1 2)
                RightCategoryPanel
            )
        )
    )
)

(Layout LeftRepPanel
    (Components
        (Table                          ExistingRepTable)
    )
    (Resources
        (ExistingRepTable.Columns       44)
        (ExistingRepTable.MinRows       8)
        (ExistingRepTable.VisibleRows   14)
        (ExistingRepTable.RowNames      "rep_0")
        (ExistingRepTable.ColumnNames   "ACTIVE"
                                        "ENAME"
                                        "EITEMS")
        (ExistingRepTable.RowLabels     "")
        (ExistingRepTable.ColumnLabels  "Active"
                                        "Simplified Rep"
                                        "Included")
        (ExistingRepTable.ColumnWidths  6
                                        30
                                        8)
        (ExistingRepTable.ShowGrid      True)
        (ExistingRepTable.TopOffset     4)
        (ExistingRepTable.BottomOffset  4)
        (ExistingRepTable.LeftOffset    4)
        (ExistingRepTable.RightOffset   4)
        (ExistingRepTable.AttachLeft    True)
        (ExistingRepTable.AttachRight   True)
        (ExistingRepTable.AttachTop     True)
        (ExistingRepTable.AttachBottom  True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                (Pos 1 1)
                ExistingRepTable
            )
        )
    )
)

(Layout RightCategoryPanel
    (Components
        (Table                          CategoryTable)
    )
    (Resources
        (CategoryTable.Columns          100)
        (CategoryTable.MinRows          8)
        (CategoryTable.VisibleRows      14)
        (CategoryTable.RowNames         "qsr_0")
        (CategoryTable.ColumnNames      "USE"
                                        "COMMON"
                                        "QTY"
                                        "STATUS")
        (CategoryTable.RowLabels        "")
        (CategoryTable.ColumnLabels     "Use"
                                        "PTC_COMMON_NAME"
                                        "Direct Components"
                                        "Status")
        (CategoryTable.ColumnWidths     8
                                        46
                                        12
                                        34)
        (CategoryTable.ShowGrid         True)
        (CategoryTable.TopOffset        4)
        (CategoryTable.BottomOffset     4)
        (CategoryTable.LeftOffset       4)
        (CategoryTable.RightOffset      4)
        (CategoryTable.AttachLeft       True)
        (CategoryTable.AttachRight      True)
        (CategoryTable.AttachTop        True)
        (CategoryTable.AttachBottom     True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                (Pos 1 1)
                CategoryTable
            )
        )
    )
)

(Layout ActionBar
    (Components
        (PushButton                     RefreshBtn)
        (PushButton                     CreatePerCategoryBtn)
        (PushButton                     CreateMergedBtn)
        (PushButton                     UpdateCurrentBtn)
        (InputPanel                     RenameInput)
        (PushButton                     RenameBtn)
        (PushButton                     CloseBtn)
    )
    (Resources
        (RefreshBtn.Label               "Refresh")
        (CreatePerCategoryBtn.Label     "Create Per Category")
        (CreateMergedBtn.Label          "Create Merged")
        (UpdateCurrentBtn.Label         "Update Current")
        (RenameInput.Columns            18)
        (RenameInput.Value              "")
        (RenameBtn.Label                "Rename")
        (CloseBtn.Label                 "Close")
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 0 0 0 1 0)
                (Pos 1 1)
                RefreshBtn
                (Pos 1 2)
                CreatePerCategoryBtn
                (Pos 1 3)
                CreateMergedBtn
                (Pos 1 4)
                UpdateCurrentBtn
                (Pos 1 5)
                RenameInput
                (Pos 1 6)
                RenameBtn
                (Pos 1 8)
                CloseBtn
            )
        )
    )
)

(TableLayout CategoryTable
    (Components
        (CheckButton                    BaseCategoryCheck)
    )
    (Resources
        (BaseCategoryCheck.Label        "")
    )
)
