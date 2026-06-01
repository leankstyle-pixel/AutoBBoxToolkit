(Dialog autobbox_dwg3_pick
    (Components
        (SubLayout                      Dwg3PickPage)
    )

    (Resources
        (.Label                         "Create 3 Views")
        (.Resizeable                    True)
        (Dwg3PickPage.AttachLeft        True)
        (Dwg3PickPage.AttachRight       True)
        (Dwg3PickPage.AttachTop         True)
        (Dwg3PickPage.AttachBottom      True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                Dwg3PickPage
            )
        )
    )
)

(Layout Dwg3PickPage
    (Components
        (SubLayout                      MainPanel)
        (SubLayout                      FooterBar)
    )

    (Resources
        (MainPanel.AttachLeft           True)
        (MainPanel.AttachRight          True)
        (MainPanel.AttachTop            True)
        (MainPanel.AttachBottom         True)
        (FooterBar.TopOffset            2)
        (FooterBar.AttachLeft           True)
        (FooterBar.AttachRight          True)
        (FooterBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1 0) (Cols 1)
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
        (LeftPanel.RightOffset          6)
        (LeftPanel.AttachLeft           True)
        (LeftPanel.AttachTop            True)
        (LeftPanel.AttachBottom         True)
        (RightPanel.LeftOffset          6)
        (RightPanel.AttachLeft          True)
        (RightPanel.AttachRight         True)
        (RightPanel.AttachTop           True)
        (RightPanel.AttachBottom        True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 1 0)
                LeftPanel
                RightPanel
            )
        )
    )
)

(Layout LeftPanel
    (Components
        (Label                          PromptLabel)
        (Label                          SimprepLabel)
        (OptionMenu                     SimprepMenu)
        (Table                          ModelList)
    )

    (Resources
        (PromptLabel.Label              "Select models for 3-view creation:")
        (PromptLabel.TopOffset          8)
        (PromptLabel.BottomOffset       4)
        (PromptLabel.LeftOffset         10)
        (PromptLabel.RightOffset        10)
        (PromptLabel.AttachLeft         True)
        (SimprepLabel.Label             "Simplified Rep")
        (SimprepLabel.TopOffset         2)
        (SimprepLabel.BottomOffset      4)
        (SimprepLabel.LeftOffset        10)
        (SimprepLabel.RightOffset       10)
        (SimprepLabel.AttachLeft        True)
        (SimprepMenu.TopOffset          2)
        (SimprepMenu.BottomOffset       6)
        (SimprepMenu.LeftOffset         10)
        (SimprepMenu.RightOffset        10)
        (SimprepMenu.Columns            22)
        (SimprepMenu.VisibleRows        6)
        (SimprepMenu.AttachLeft         True)
        (ModelList.TopOffset            2)
        (ModelList.BottomOffset         10)
        (ModelList.LeftOffset           10)
        (ModelList.RightOffset          10)
        (ModelList.AttachLeft           True)
        (ModelList.AttachRight          True)
        (ModelList.AttachTop            True)
        (ModelList.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 0 0 1) (Cols 1)
                PromptLabel
                SimprepLabel
                SimprepMenu
                ModelList
            )
        )
    )
)

(Layout RightPanel
    (Components
        (Label                          ViewPromptLabel)
        (SubLayout                      ViewGrid)
        (SubLayout                      QuickModeBar)
    )

    (Resources
        (ViewPromptLabel.Label          "Select views to create:")
        (ViewPromptLabel.TopOffset      8)
        (ViewPromptLabel.BottomOffset   2)
        (ViewPromptLabel.LeftOffset     10)
        (ViewPromptLabel.RightOffset    10)
        (ViewPromptLabel.AttachLeft     True)
        (ViewPromptLabel.AttachRight    True)
        (ViewGrid.TopOffset             2)
        (ViewGrid.LeftOffset            10)
        (ViewGrid.RightOffset           10)
        (ViewGrid.AttachLeft            True)
        (ViewGrid.AttachRight           True)
        (ViewGrid.AttachTop             True)
        (ViewGrid.AttachBottom          True)
        (QuickModeBar.TopOffset         4)
        (QuickModeBar.LeftOffset        10)
        (QuickModeBar.RightOffset       10)
        (QuickModeBar.BottomOffset      10)
        (QuickModeBar.AttachLeft        True)
        (QuickModeBar.AttachRight       True)
        (QuickModeBar.AttachBottom      True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1 0 2 0 1) (Cols 1)
                (Pos 2 1)
                ViewPromptLabel
                (Pos 3 1)
                ViewGrid
                (Pos 4 1)
                QuickModeBar
            )
        )
    )
)

(Layout ViewGrid
    (Components
        (CheckButton                    BackCheck)
        (CheckButton                    TopCheck)
        (CheckButton                    RightCheck)
        (CheckButton                    FrontCheck)
        (CheckButton                    LeftCheck)
        (CheckButton                    BottomCheck)
        (CheckButton                    IsoCheck)
    )

    (Resources
        (BackCheck.Label                "Back")
        (BackCheck.TopOffset            1)
        (BackCheck.BottomOffset         1)
        (BackCheck.LeftOffset           1)
        (BackCheck.RightOffset          1)
        (BackCheck.AttachLeft           True)
        (BackCheck.AttachRight          True)
        (BackCheck.AttachTop            True)
        (BackCheck.AttachBottom         True)
        (TopCheck.Label                 "Top")
        (TopCheck.TopOffset             1)
        (TopCheck.BottomOffset          1)
        (TopCheck.LeftOffset            1)
        (TopCheck.RightOffset           1)
        (TopCheck.AttachLeft            True)
        (TopCheck.AttachRight           True)
        (TopCheck.AttachTop             True)
        (TopCheck.AttachBottom          True)
        (RightCheck.Label               "Right")
        (RightCheck.TopOffset           1)
        (RightCheck.BottomOffset        1)
        (RightCheck.LeftOffset          1)
        (RightCheck.RightOffset         1)
        (RightCheck.AttachLeft          True)
        (RightCheck.AttachRight         True)
        (RightCheck.AttachTop           True)
        (RightCheck.AttachBottom        True)
        (FrontCheck.Label               "Front")
        (FrontCheck.TopOffset           1)
        (FrontCheck.BottomOffset        1)
        (FrontCheck.LeftOffset          1)
        (FrontCheck.RightOffset         1)
        (FrontCheck.AttachLeft          True)
        (FrontCheck.AttachRight         True)
        (FrontCheck.AttachTop           True)
        (FrontCheck.AttachBottom        True)
        (LeftCheck.Label                "Left")
        (LeftCheck.TopOffset            1)
        (LeftCheck.BottomOffset         1)
        (LeftCheck.LeftOffset           1)
        (LeftCheck.RightOffset          1)
        (LeftCheck.AttachLeft           True)
        (LeftCheck.AttachRight          True)
        (LeftCheck.AttachTop            True)
        (LeftCheck.AttachBottom         True)
        (BottomCheck.Label              "Bottom")
        (BottomCheck.TopOffset          1)
        (BottomCheck.BottomOffset       1)
        (BottomCheck.LeftOffset         1)
        (BottomCheck.RightOffset        1)
        (BottomCheck.AttachLeft         True)
        (BottomCheck.AttachRight        True)
        (BottomCheck.AttachTop          True)
        (BottomCheck.AttachBottom       True)
        (IsoCheck.Label                 "Isometric")
        (IsoCheck.TopOffset             1)
        (IsoCheck.BottomOffset          1)
        (IsoCheck.LeftOffset            1)
        (IsoCheck.RightOffset           1)
        (IsoCheck.AttachLeft            True)
        (IsoCheck.AttachRight           True)
        (IsoCheck.AttachTop             True)
        (IsoCheck.AttachBottom          True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.Layout
            (Grid (Rows 1 1 1) (Cols 1 1 1)
                (Pos 1 2)
                TopCheck
                (Pos 1 3)
                BackCheck
                (Pos 2 1)
                LeftCheck
                (Pos 2 2)
                FrontCheck
                (Pos 2 3)
                RightCheck
                (Pos 3 2)
                BottomCheck
                (Pos 3 3)
                IsoCheck
            )
        )
    )
)

(Layout QuickModeBar
    (Components
        (Label                          FrameLabel)
        (OptionMenu                     FrameModeMenu)
        (CheckButton                    QuickModeCheck)
    )

    (Resources
        (FrameLabel.Label               "Frame")
        (FrameLabel.TopOffset           4)
        (FrameLabel.BottomOffset        2)
        (FrameLabel.LeftOffset          10)
        (FrameLabel.RightOffset         10)
        (FrameLabel.AttachLeft          True)
        (FrameModeMenu.TopOffset        0)
        (FrameModeMenu.BottomOffset     4)
        (FrameModeMenu.LeftOffset       10)
        (FrameModeMenu.RightOffset      10)
        (FrameModeMenu.Columns          24)
        (FrameModeMenu.VisibleRows      3)
        (FrameModeMenu.AttachLeft       True)
        (QuickModeCheck.Label           "Fast Mode")
        (QuickModeCheck.TopOffset       4)
        (QuickModeCheck.BottomOffset    10)
        (QuickModeCheck.LeftOffset      10)
        (QuickModeCheck.RightOffset     10)
        (QuickModeCheck.AttachLeft      True)
        (QuickModeCheck.AttachRight     True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 0 0) (Cols 1)
                FrameLabel
                FrameModeMenu
                QuickModeCheck
            )
        )
    )
)

(TableLayout ModelList
    (Components
        (CheckButton                    BaseModelCheck)
    )

    (Resources
        (BaseModelCheck.Label           "")
    )
)

(Layout FooterBar
    (Components
        (PushButton                     SelectAllBtn)
        (PushButton                     ClearBtn)
        (PushButton                     CancelBtn)
        (PushButton                     OKBtn)
    )

    (Resources
        (SelectAllBtn.Label             "Select All")
        (SelectAllBtn.TopOffset         6)
        (SelectAllBtn.BottomOffset      8)
        (SelectAllBtn.LeftOffset        10)
        (SelectAllBtn.RightOffset       4)
        (SelectAllBtn.UseStandardWidth  1)
        (SelectAllBtn.AttachLeft        True)
        (SelectAllBtn.AttachBottom      True)
        (ClearBtn.Label                 "Clear")
        (ClearBtn.TopOffset             6)
        (ClearBtn.BottomOffset          8)
        (ClearBtn.LeftOffset            0)
        (ClearBtn.RightOffset           4)
        (ClearBtn.UseStandardWidth      1)
        (ClearBtn.AttachBottom          True)
        (CancelBtn.Label                "Cancel")
        (CancelBtn.TopOffset            6)
        (CancelBtn.BottomOffset         8)
        (CancelBtn.LeftOffset           4)
        (CancelBtn.RightOffset          4)
        (CancelBtn.UseStandardWidth     1)
        (CancelBtn.AttachBottom         True)
        (OKBtn.Label                    "OK")
        (OKBtn.TopOffset                6)
        (OKBtn.BottomOffset             8)
        (OKBtn.LeftOffset               0)
        (OKBtn.RightOffset              10)
        (OKBtn.UseStandardWidth         1)
        (OKBtn.AttachRight              True)
        (OKBtn.AttachBottom             True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0) (Cols 0 0 1 0 0)
                (Pos 1 1)
                SelectAllBtn
                (Pos 1 2)
                ClearBtn
                (Pos 1 4)
                CancelBtn
                (Pos 1 5)
                OKBtn
            )
        )
    )
)
