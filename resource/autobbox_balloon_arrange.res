(Dialog autobbox_balloon_arrange
    (Components
        (SubLayout                      BalloonArrangePage)
    )
    (Resources
        (.Label                         "Arrange Balloons")
        (.Resizeable                    False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                BalloonArrangePage
            )
        )
    )
)

(Layout BalloonArrangePage
    (Components
        (Label                          PromptLabel)
        (Label                          SourceLabel)
        (OptionMenu                     SourceMenu)
        (Label                          ParamLabel)
        (InputPanel                     ParamInput)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )
    (Resources
        (PromptLabel.Label              "Select any BOM table cell. Note balloons will be generated from BOM repeat-region components.")
        (SourceLabel.Label              "Balloon text")
        (SourceMenu.Columns             18)
        (SourceMenu.VisibleRows         2)
        (ParamLabel.Label               "Parameter")
        (ParamInput.Columns             32)
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                SourceLabel
                (Pos 2 2)
                SourceMenu
                (Pos 3 1)
                ParamLabel
                (Pos 3 2)
                ParamInput
                (Pos 4 1)
                OKBtn
                (Pos 4 2)
                CancelBtn
            )
        )
    )
)
