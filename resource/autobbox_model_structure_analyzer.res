(Dialog autobbox_model_structure_analyzer
    (Components
        (SubLayout                      MainPage)
    )

    (Resources
        (.Label                         "Model Structure Analyzer")
        (.Resizeable                    True)
        (MainPage.AttachLeft            True)
        (MainPage.AttachRight           True)
        (MainPage.AttachTop             True)
        (MainPage.AttachBottom          True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                MainPage
            )
        )
    )
)

(Layout MainPage
    (Components
        (Label                          SummaryLabel)
        (SubLayout                      BodyPanel)
        (Label                          StatusLabel)
        (SubLayout                      ButtonBar)
    )

    (Resources
        (SummaryLabel.Label             "Model structure summary")
        (SummaryLabel.TopOffset         8)
        (SummaryLabel.BottomOffset      4)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (SummaryLabel.AttachLeft        True)
        (SummaryLabel.AttachRight       True)
        (SummaryLabel.AttachTop         True)
        (BodyPanel.TopOffset            4)
        (BodyPanel.BottomOffset         4)
        (BodyPanel.LeftOffset           8)
        (BodyPanel.RightOffset          8)
        (BodyPanel.AttachLeft           True)
        (BodyPanel.AttachRight          True)
        (BodyPanel.AttachTop            True)
        (BodyPanel.AttachBottom         True)
        (StatusLabel.Label              "Select a model node.")
        (StatusLabel.TopOffset          4)
        (StatusLabel.BottomOffset       4)
        (StatusLabel.LeftOffset         8)
        (StatusLabel.RightOffset        8)
        (StatusLabel.AttachLeft         True)
        (StatusLabel.AttachRight        True)
        (ButtonBar.TopOffset            4)
        (ButtonBar.BottomOffset         8)
        (ButtonBar.LeftOffset           8)
        (ButtonBar.RightOffset          8)
        (ButtonBar.AttachLeft           True)
        (ButtonBar.AttachRight          True)
        (ButtonBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0 0) (Cols 1)
                (Pos 1 1)
                SummaryLabel
                (Pos 2 1)
                BodyPanel
                (Pos 3 1)
                StatusLabel
                (Pos 4 1)
                ButtonBar
            )
        )
    )
)

(Layout BodyPanel
    (Components
        (Table                          NodeTable)
        (SubLayout                      DetailPanel)
    )

    (Resources
        (NodeTable.TopOffset            4)
        (NodeTable.BottomOffset         4)
        (NodeTable.LeftOffset           4)
        (NodeTable.RightOffset          4)
        (NodeTable.AttachLeft           True)
        (NodeTable.AttachTop            True)
        (NodeTable.AttachBottom         True)
        (DetailPanel.TopOffset          4)
        (DetailPanel.BottomOffset       4)
        (DetailPanel.LeftOffset         4)
        (DetailPanel.RightOffset        4)
        (DetailPanel.AttachLeft         True)
        (DetailPanel.AttachRight        True)
        (DetailPanel.AttachTop          True)
        (DetailPanel.AttachBottom       True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 1)
                (Pos 1 1)
                NodeTable
                (Pos 1 2)
                DetailPanel
            )
        )
    )
)

(Layout DetailPanel
    (Components
        (Label                          DetailLabel)
        (Table                          DetailTable)
    )

    (Resources
        (DetailLabel.Label              "Details")
        (DetailLabel.TopOffset          2)
        (DetailLabel.BottomOffset       2)
        (DetailLabel.LeftOffset         4)
        (DetailLabel.RightOffset        4)
        (DetailLabel.AttachLeft         True)
        (DetailLabel.AttachRight        True)
        (DetailLabel.AttachTop          True)
        (DetailTable.TopOffset          2)
        (DetailTable.BottomOffset       2)
        (DetailTable.LeftOffset         4)
        (DetailTable.RightOffset        4)
        (DetailTable.AttachLeft         True)
        (DetailTable.AttachRight        True)
        (DetailTable.AttachTop          True)
        (DetailTable.AttachBottom       True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1) (Cols 1)
                (Pos 1 1)
                DetailLabel
                (Pos 2 1)
                DetailTable
            )
        )
    )
)

(Layout ButtonBar
    (Components
        (PushButton                     OverviewBtn)
        (PushButton                     ConstraintsBtn)
        (PushButton                     ParamsBtn)
        (PushButton                     FamilyBtn)
        (PushButton                     RelationsBtn)
        (PushButton                     FeaturesBtn)
        (PushButton                     DimsBtn)
        (PushButton                     RefsBtn)
        (PushButton                     SaveJsonBtn)
        (PushButton                     RefreshBtn)
        (PushButton                     CloseBtn)
    )

    (Resources
        (OverviewBtn.Label              "Overview")
        (ConstraintsBtn.Label           "Constraints")
        (ParamsBtn.Label                "Parameters")
        (FamilyBtn.Label                "Family Table")
        (RelationsBtn.Label             "Relations")
        (FeaturesBtn.Label              "Features")
        (DimsBtn.Label                  "Dimensions")
        (RefsBtn.Label                  "References")
        (SaveJsonBtn.Label              "Save JSON")
        (RefreshBtn.Label               "Refresh")
        (CloseBtn.Label                 "Close")
        (CloseBtn.AttachRight           True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 0 0 0 0 0 1 0 0)
                (Pos 1 1)
                OverviewBtn
                (Pos 1 2)
                ConstraintsBtn
                (Pos 1 3)
                ParamsBtn
                (Pos 1 4)
                FamilyBtn
                (Pos 1 5)
                RelationsBtn
                (Pos 1 6)
                FeaturesBtn
                (Pos 1 7)
                DimsBtn
                (Pos 1 8)
                RefsBtn
                (Pos 1 9)
                SaveJsonBtn
                (Pos 1 10)
                RefreshBtn
                (Pos 1 11)
                CloseBtn
            )
        )
    )
)
