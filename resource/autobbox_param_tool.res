(Dialog autobbox_param_tool
    (Components
        (SubLayout                      BomToolPage)
    )

    (Resources
        (.Label                         "BOM")
        (.Resizeable                    True)
        (BomToolPage.AttachLeft         True)
        (BomToolPage.AttachRight        True)
        (BomToolPage.AttachTop          True)
        (BomToolPage.AttachBottom       True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                BomToolPage
            )
        )
    )
)

(Layout BomToolPage
    (Components
        (Label                          SummaryLabel)
        (SubLayout                      MainPanel)
        (SubLayout                      FooterBar)
    )

    (Resources
        (SummaryLabel.Label             "BOM: 0")
        (SummaryLabel.TopOffset         4)
        (SummaryLabel.BottomOffset      2)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (MainPanel.AttachLeft           True)
        (MainPanel.AttachRight          True)
        (MainPanel.AttachTop            True)
        (MainPanel.AttachBottom         True)
        (FooterBar.AttachLeft           True)
        (FooterBar.AttachRight          True)
        (FooterBar.AttachBottom         True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                SummaryLabel
                MainPanel
                FooterBar
            )
        )
    )
)

(Layout MainPanel
    (Components
        (SubLayout                      LeftPanel)
        (SubLayout                      RightPanel)
    )

    (Resources
        (LeftPanel.AttachLeft           True)
        (LeftPanel.AttachTop            True)
        (LeftPanel.AttachBottom         True)
        (RightPanel.AttachLeft          True)
        (RightPanel.AttachRight         True)
        (RightPanel.AttachTop           True)
        (RightPanel.AttachBottom        True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 1)
                LeftPanel
                RightPanel
            )
        )
    )
)

(Layout LeftPanel
    (Components
        (Label                          AvailableLabel)
        (Table                          ParamList)
        (SubLayout                      CreateBar)
    )

    (Resources
        (AvailableLabel.Label           "Available Params")
        (AvailableLabel.TopOffset       2)
        (AvailableLabel.BottomOffset    2)
        (AvailableLabel.LeftOffset      4)
        (AvailableLabel.RightOffset     4)
        (ParamList.Columns              40)
        (ParamList.MinRows              10)
        (ParamList.VisibleRows          14)
        (ParamList.RowNames             "P_0")
        (ParamList.ColumnNames          "USE"
                                        "NAME"
                                        "TYPE"
                                        "HIT")
        (ParamList.RowLabels            "")
        (ParamList.ColumnLabels         "Use"
                                        "Name"
                                        "Type"
                                        "Hit")
        (ParamList.ColumnWidths         4
                                        18
                                        12
                                        6)
        (ParamList.ShowGrid             True)
        (ParamList.TopOffset            4)
        (ParamList.BottomOffset         4)
        (ParamList.LeftOffset           4)
        (ParamList.RightOffset          4)
        (ParamList.AttachLeft           True)
        (ParamList.AttachTop            True)
        (ParamList.AttachBottom         True)
        (CreateBar.AttachLeft           True)
        (CreateBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                AvailableLabel
                ParamList
                CreateBar
            )
        )
    )
)

(Layout CreateBar
    (Components
        (SubLayout                      ParamFormGrid)
        (SubLayout                      ParamButtonBar)
        (Label                          CreateBoolValueLabel)
        (OptionMenu                     CreateBoolValueMenu)
    )

    (Resources
        (ParamFormGrid.AttachLeft       True)
        (ParamFormGrid.AttachRight      True)
        (ParamButtonBar.AttachLeft      True)
        (ParamButtonBar.AttachRight     True)
        (ParamButtonBar.AttachBottom    True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1 0) (Cols 1)
                ParamFormGrid
                ParamButtonBar
            )
        )
    )
)

(Layout ParamFormGrid
    (Components
        (Label                          ParamNameLabel)
        (InputPanel                     CreateNameInput)
        (Label                          ParamTypeLabel)
        (OptionMenu                     CreateTypeMenu)
        (Label                          ParamDefaultValueLabel)
        (InputPanel                     ParamDefaultValueInput)
        (Label                          ParamOptionValueLabel)
        (OptionMenu                     ParamOptionValueMenu)
    )

    (Resources
        (ParamNameLabel.Label           "Param Name")
        (ParamTypeLabel.Label           "Param Type")
        (ParamDefaultValueLabel.Label   "Default")
        (ParamOptionValueLabel.Label    "Options")
        (CreateNameInput.Columns        18)
        (CreateTypeMenu.Columns         18)
        (CreateTypeMenu.VisibleRows     4)
        (ParamDefaultValueInput.Columns 18)
        (ParamOptionValueMenu.Columns   18)
        (ParamOptionValueMenu.VisibleRows 1)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.Layout
            (Grid (Rows 0 0 0 0) (Cols 0 1)
                ParamNameLabel          CreateNameInput
                ParamTypeLabel          CreateTypeMenu
                ParamDefaultValueLabel  ParamDefaultValueInput
                ParamOptionValueLabel   ParamOptionValueMenu
            )
        )
    )
)

(Layout ParamButtonBar
    (Components
        (PushButton                     ParamDeleteBtn)
        (PushButton                     ParamUpdateBtn)
        (PushButton                     ParamAddBtn)
    )

    (Resources
        (ParamDeleteBtn.Label           "Delete")
        (ParamDeleteBtn.UseStandardWidth 1)
        (ParamUpdateBtn.Label           "Edit")
        (ParamUpdateBtn.UseStandardWidth 1)
        (ParamAddBtn.Label              "Add")
        (ParamAddBtn.UseStandardWidth   1)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0) (Cols 1 1 1)
                ParamDeleteBtn
                ParamUpdateBtn
                ParamAddBtn
            )
        )
    )
)
(Layout RightPanel
    (Components
        (SubLayout                      SimprepBar)
        (Label                          TableLabel)
        (Table                          BomTable)
    )

    (Resources
        (TableLabel.Label               "BOM Table")
        (TableLabel.TopOffset           2)
        (TableLabel.BottomOffset        2)
        (TableLabel.LeftOffset          4)
        (TableLabel.RightOffset         4)
        (BomTable.Columns               96)
        (BomTable.MinRows               10)
        (BomTable.VisibleRows           15)
        (BomTable.RowNames              "ROW_0")
        (BomTable.ColumnNames           "EDIT"
                                        "SEQ"
                                        "LEVEL"
                                        "MODEL"
                                        "QTY")
        (BomTable.RowLabels             "")
        (BomTable.ColumnLabels          "☑"
                                        "Seq"
                                        "Model"
                                        "Qty")
        (BomTable.ColumnWidths          4
                                        6
                                        6
                                        28
                                        8)
        (BomTable.ShowGrid              True)
        (BomTable.TopOffset             4)
        (BomTable.BottomOffset          4)
        (BomTable.LeftOffset            4)
        (BomTable.RightOffset           4)
        (BomTable.AttachLeft            True)
        (BomTable.AttachRight           True)
        (BomTable.AttachTop             True)
        (SimprepBar.AttachLeft          True)
        (SimprepBar.AttachRight         True)
        (SimprepBar.AttachTop           True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 0 1) (Cols 1)
                SimprepBar
                TableLabel
                BomTable
            )
        )
    )
)

(Layout SimprepBar
    (Components
        (CheckButton                    BomSelectAllCheck)
        (OptionMenu                     MaxLevelMenu)
        (CheckButton                    AssembliesFilterCheck)
        (CheckButton                    PartsFilterCheck)
        (Label                          SimprepLabel)
        (OptionMenu                     SimprepMenu)
        (Label                          BomModelFilterLabel)
        (InputPanel                     BomModelFilterInput)
        (Label                          BomParamFilterLabel)
        (InputPanel                     BomParamFilterInput)
        (Label                          BomValueFilterLabel)
        (InputPanel                     BomValueFilterInput)
    )

    (Resources
        (BomSelectAllCheck.Label        "Select All")
        (BomSelectAllCheck.TopOffset    2)
        (BomSelectAllCheck.BottomOffset 2)
        (BomSelectAllCheck.LeftOffset   4)
        (BomSelectAllCheck.RightOffset  8)
        (MaxLevelMenu.Columns           8)
        (MaxLevelMenu.VisibleRows       10)
        (MaxLevelMenu.TopOffset         2)
        (MaxLevelMenu.BottomOffset      2)
        (MaxLevelMenu.LeftOffset        0)
        (MaxLevelMenu.RightOffset       8)
        (AssembliesFilterCheck.Label    "Asm")
        (AssembliesFilterCheck.TopOffset 2)
        (AssembliesFilterCheck.BottomOffset 2)
        (AssembliesFilterCheck.LeftOffset 0)
        (AssembliesFilterCheck.RightOffset 4)
        (PartsFilterCheck.Label         "Part")
        (PartsFilterCheck.TopOffset     2)
        (PartsFilterCheck.BottomOffset  2)
        (PartsFilterCheck.LeftOffset    0)
        (PartsFilterCheck.RightOffset   8)
        (SimprepLabel.Label             "Simplified Rep")
        (SimprepLabel.TopOffset         2)
        (SimprepLabel.BottomOffset      2)
        (SimprepLabel.LeftOffset        4)
        (SimprepLabel.RightOffset       4)
        (SimprepMenu.Columns            24)
        (SimprepMenu.VisibleRows        8)
        (SimprepMenu.TopOffset          2)
        (SimprepMenu.BottomOffset       2)
        (SimprepMenu.LeftOffset         0)
        (SimprepMenu.RightOffset        4)
        (BomModelFilterLabel.Label      "Model")
        (BomModelFilterLabel.TopOffset  2)
        (BomModelFilterLabel.BottomOffset 2)
        (BomModelFilterLabel.LeftOffset 4)
        (BomModelFilterLabel.RightOffset 4)
        (BomModelFilterInput.Columns    18)
        (BomModelFilterInput.TopOffset  2)
        (BomModelFilterInput.BottomOffset 2)
        (BomModelFilterInput.LeftOffset 0)
        (BomModelFilterInput.RightOffset 8)
        (BomParamFilterLabel.Label      "Param")
        (BomParamFilterLabel.TopOffset  2)
        (BomParamFilterLabel.BottomOffset 2)
        (BomParamFilterLabel.LeftOffset 0)
        (BomParamFilterLabel.RightOffset 4)
        (BomParamFilterInput.Columns    14)
        (BomParamFilterInput.TopOffset  2)
        (BomParamFilterInput.BottomOffset 2)
        (BomParamFilterInput.LeftOffset 0)
        (BomParamFilterInput.RightOffset 8)
        (BomValueFilterLabel.Label      "Value")
        (BomValueFilterLabel.TopOffset  2)
        (BomValueFilterLabel.BottomOffset 2)
        (BomValueFilterLabel.LeftOffset 0)
        (BomValueFilterLabel.RightOffset 4)
        (BomValueFilterInput.Columns    18)
        (BomValueFilterInput.TopOffset  2)
        (BomValueFilterInput.BottomOffset 2)
        (BomValueFilterInput.LeftOffset 0)
        (BomValueFilterInput.RightOffset 4)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 0) (Cols 0 0 0 0 0 1)
                BomSelectAllCheck
                MaxLevelMenu
                AssembliesFilterCheck
                PartsFilterCheck
                SimprepLabel
                SimprepMenu
                BomModelFilterLabel
                BomModelFilterInput
                BomParamFilterLabel
                BomParamFilterInput
                BomValueFilterLabel
                BomValueFilterInput
            )
        )
    )
)

(Layout FooterBar
    (Components
        (PushButton                     RefreshBtn)
        (PushButton                     MoveLeftBtn)
        (PushButton                     MoveRightBtn)
        (PushButton                     UpdateBtn)
        (PushButton                     ExportBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (RefreshBtn.Label               "Refresh")
        (RefreshBtn.TopOffset           4)
        (RefreshBtn.BottomOffset        6)
        (RefreshBtn.LeftOffset          4)
        (RefreshBtn.RightOffset         4)
        (RefreshBtn.UseStandardWidth    1)
        (MoveLeftBtn.Label              "←")
        (MoveLeftBtn.TopOffset          4)
        (MoveLeftBtn.BottomOffset       6)
        (MoveLeftBtn.LeftOffset         0)
        (MoveLeftBtn.RightOffset        4)
        (MoveLeftBtn.UseStandardWidth   1)
        (MoveRightBtn.Label             "→")
        (MoveRightBtn.TopOffset         4)
        (MoveRightBtn.BottomOffset      6)
        (MoveRightBtn.LeftOffset        0)
        (MoveRightBtn.RightOffset       4)
        (MoveRightBtn.UseStandardWidth  1)
        (UpdateBtn.Label                "Update")
        (UpdateBtn.TopOffset            4)
        (UpdateBtn.BottomOffset         6)
        (UpdateBtn.LeftOffset           0)
        (UpdateBtn.RightOffset          4)
        (UpdateBtn.UseStandardWidth     1)
        (ExportBtn.Label                "Export")
        (ExportBtn.TopOffset            4)
        (ExportBtn.BottomOffset         6)
        (ExportBtn.LeftOffset           0)
        (ExportBtn.RightOffset          4)
        (ExportBtn.UseStandardWidth     1)
        (CancelBtn.Label                "Close")
        (CancelBtn.TopOffset            4)
        (CancelBtn.BottomOffset         6)
        (CancelBtn.LeftOffset           0)
        (CancelBtn.RightOffset          4)
        (CancelBtn.UseStandardWidth     1)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0) (Cols 1 0 0 0 0 0)
                RefreshBtn
                MoveLeftBtn
                MoveRightBtn
                UpdateBtn
                ExportBtn
                CancelBtn
            )
        )
    )
)
(TableLayout ParamList
    (Components
        (CheckButton                    BaseAvailCheck)
    )

    (Resources
        (BaseAvailCheck.Label           "")
    )
)

(TableLayout BomTable
    (Components
        (CheckButton                    BaseBomRowCheck)
        (InputPanel                     CellInputBase)
        (OptionMenu                     CellBoolBase)
    )

    (Resources
        (BaseBomRowCheck.Label          "")
        (CellInputBase.Columns          20)
        (CellBoolBase.Columns           8)
        (CellBoolBase.VisibleRows       2)
    )
)
