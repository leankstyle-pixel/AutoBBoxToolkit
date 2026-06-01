(Dialog autobbox_random_color
    (Components
        (SubLayout                      RandomColorPage)
    )

    (Resources
        (.Label                         "Random Colors")
        (.Resizeable                    True)
        (RandomColorPage.AttachLeft     True)
        (RandomColorPage.AttachRight    True)
        (RandomColorPage.AttachTop      True)
        (RandomColorPage.AttachBottom   True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                RandomColorPage
            )
        )
    )
)

(Layout RandomColorPage
    (Components
        (SubLayout                      HeaderBar)
        (Table                          ModelTable)
        (SubLayout                      FooterBar)
    )

    (Resources
        (HeaderBar.TopOffset            8)
        (HeaderBar.LeftOffset           6)
        (HeaderBar.RightOffset          6)
        (HeaderBar.AttachLeft           True)
        (HeaderBar.AttachRight          True)
        (HeaderBar.AttachTop            True)
        (ModelTable.TopOffset           2)
        (ModelTable.BottomOffset        6)
        (ModelTable.LeftOffset          6)
        (ModelTable.RightOffset         6)
        (ModelTable.AttachLeft          True)
        (ModelTable.AttachRight         True)
        (ModelTable.AttachTop           True)
        (ModelTable.AttachBottom        True)
        (FooterBar.TopOffset            4)
        (FooterBar.LeftOffset           6)
        (FooterBar.RightOffset          6)
        (FooterBar.BottomOffset         6)
        (FooterBar.AttachLeft           True)
        (FooterBar.AttachRight          True)
        (FooterBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                (Pos 1 1)
                HeaderBar
                (Pos 2 1)
                ModelTable
                (Pos 3 1)
                FooterBar
            )
        )
    )
)

(Layout HeaderBar
    (Components
        (Label                          PromptLabel)
        (Label                          ModeLabel)
        (OptionMenu                     ModeMenu)
        (Label                          ParamLabel)
        (InputPanel                     ParamInput)
        (Label                          FileLabel)
        (Label                          FileValueLabel)
        (PushButton                     BrowseBtn)
    )

    (Resources
        (PromptLabel.Label              "Select models, mode, and a .dmt color map:")
        (PromptLabel.BottomOffset       4)
        (PromptLabel.LeftOffset         4)
        (PromptLabel.RightOffset        4)
        (PromptLabel.AttachLeft         True)
        (PromptLabel.AttachRight        True)
        (ModeLabel.Label                "Mode")
        (ModeLabel.TopOffset            2)
        (ModeLabel.BottomOffset         2)
        (ModeLabel.LeftOffset           4)
        (ModeLabel.AttachLeft           True)
        (ModeMenu.TopOffset             2)
        (ModeMenu.BottomOffset          2)
        (ModeMenu.LeftOffset            4)
        (ModeMenu.RightOffset           8)
        (ParamLabel.Label               "Color Parameter")
        (ParamLabel.TopOffset           2)
        (ParamLabel.BottomOffset        2)
        (ParamLabel.LeftOffset          4)
        (ParamInput.Columns             20)
        (ParamInput.MaxLen              80)
        (ParamInput.TopOffset           2)
        (ParamInput.BottomOffset        2)
        (ParamInput.LeftOffset          4)
        (ParamInput.RightOffset         4)
        (ParamInput.AttachRight         True)
        (FileLabel.Label                "Color Map (.dmt)")
        (FileLabel.TopOffset            2)
        (FileLabel.BottomOffset         2)
        (FileLabel.LeftOffset           4)
        (FileLabel.AttachLeft           True)
        (FileValueLabel.Label           "<No .dmt file selected>")
        (FileValueLabel.TopOffset       2)
        (FileValueLabel.BottomOffset    2)
        (FileValueLabel.LeftOffset      4)
        (FileValueLabel.RightOffset     4)
        (FileValueLabel.AttachLeft      True)
        (FileValueLabel.AttachRight     True)
        (BrowseBtn.Label                "Browse DMT")
        (BrowseBtn.TopOffset            2)
        (BrowseBtn.BottomOffset         2)
        (BrowseBtn.RightOffset          4)
        (BrowseBtn.AttachRight          True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.Layout
            (Grid (Rows 0 0 0) (Cols 0 1 0 1 0)
                (Pos 1 1)
                (Span 1 5)
                PromptLabel
                (Pos 2 1)
                ModeLabel
                (Pos 2 2)
                ModeMenu
                (Pos 2 3)
                ParamLabel
                (Pos 2 4)
                (Span 1 2)
                ParamInput
                (Pos 3 1)
                FileLabel
                (Pos 3 2)
                (Span 1 3)
                FileValueLabel
                (Pos 3 5)
                BrowseBtn
            )
        )
    )
)

(Layout FooterBar
    (Components
        (PushButton                     SelectAllBtn)
        (PushButton                     ClearBtn)
        (PushButton                     RandomizeBtn)
        (PushButton                     RefreshBtn)
        (PushButton                     AddLibraryBtn)
        (PushButton                     WriteParamBtn)
        (PushButton                     ClearColorsBtn)
        (PushButton                     CancelBtn)
        (PushButton                     OKBtn)
    )

    (Resources
        (SelectAllBtn.Label             "Select All")
        (SelectAllBtn.TopOffset         4)
        (SelectAllBtn.BottomOffset      2)
        (SelectAllBtn.LeftOffset        4)
        (ClearBtn.Label                 "Clear Selection")
        (ClearBtn.TopOffset             4)
        (ClearBtn.BottomOffset          2)
        (RandomizeBtn.Label             "Randomize")
        (RandomizeBtn.TopOffset         4)
        (RandomizeBtn.BottomOffset      2)
        (RefreshBtn.Label               "Refresh")
        (RefreshBtn.TopOffset           4)
        (RefreshBtn.BottomOffset        2)
        (AddLibraryBtn.Label            "Add to Library")
        (AddLibraryBtn.TopOffset        4)
        (AddLibraryBtn.BottomOffset     2)
        (WriteParamBtn.Label            "Write Param")
        (WriteParamBtn.TopOffset        4)
        (WriteParamBtn.BottomOffset     2)
        (ClearColorsBtn.Label           "Clear Colors")
        (ClearColorsBtn.TopOffset       4)
        (ClearColorsBtn.BottomOffset    2)
        (CancelBtn.Label                "Cancel")
        (CancelBtn.TopOffset            4)
        (CancelBtn.BottomOffset         2)
        (OKBtn.Label                    "Apply")
        (OKBtn.TopOffset                4)
        (OKBtn.BottomOffset             2)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 0 0 0 0 1 0 0)
                (Pos 1 1)
                SelectAllBtn
                (Pos 1 2)
                ClearBtn
                (Pos 1 3)
                RandomizeBtn
                (Pos 1 4)
                RefreshBtn
                (Pos 1 5)
                AddLibraryBtn
                (Pos 1 6)
                WriteParamBtn
                (Pos 1 7)
                ClearColorsBtn
                (Pos 1 9)
                CancelBtn
                (Pos 1 10)
                OKBtn
            )
        )
    )
)

(TableLayout ModelTable
    (Components
        (CheckButton                    BaseModelCheck)
    )

    (Resources
        (BaseModelCheck.Label           "")
    )
)
