(Dialog autobbox_drawing_scale_sync
    (Components
        (SubLayout                      ScaleSyncPage)
    )

    (Resources
        (.Label                         "Drawing Scale Sync")
        (.Resizeable                    False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                ScaleSyncPage
            )
        )
    )
)

(Layout ScaleSyncPage
    (Components
        (Label                          PromptLabel)
        (Label                          ScopeLabel)
        (RadioGroup                     ScopeGroup)
        (Label                          ScaleLabel)
        (InputPanel                     ScaleInput)
        (Label                          HelpLabel)
        (SubLayout                      FooterBar)
    )

    (Resources
        (PromptLabel.Label              "Synchronize drawing view scale")
        (PromptLabel.TopOffset          10)
        (PromptLabel.BottomOffset       6)
        (PromptLabel.LeftOffset         10)
        (PromptLabel.RightOffset        10)
        (PromptLabel.AttachLeft         True)
        (PromptLabel.AttachRight        True)
        (ScopeLabel.Label               "Scope:")
        (ScopeLabel.TopOffset           4)
        (ScopeLabel.BottomOffset        2)
        (ScopeLabel.LeftOffset          10)
        (ScopeLabel.RightOffset         10)
        (ScopeLabel.AttachLeft          True)
        (ScopeGroup.Names               "current" "all")
        (ScopeGroup.Labels              "Current sheet" "All sheets")
        (ScopeGroup.TopOffset           2)
        (ScopeGroup.BottomOffset        8)
        (ScopeGroup.LeftOffset          12)
        (ScopeGroup.RightOffset         12)
        (ScopeGroup.AttachLeft          True)
        (ScaleLabel.Label               "Sheet scale value:")
        (ScaleLabel.TopOffset           4)
        (ScaleLabel.BottomOffset        2)
        (ScaleLabel.LeftOffset          10)
        (ScaleLabel.RightOffset         10)
        (ScaleLabel.AttachLeft          True)
        (ScaleInput.Columns             18)
        (ScaleInput.MaxLen              32)
        (ScaleInput.TopOffset           2)
        (ScaleInput.BottomOffset        6)
        (ScaleInput.LeftOffset          12)
        (ScaleInput.RightOffset         12)
        (ScaleInput.AttachLeft          True)
        (ScaleInput.AttachRight         True)
        (HelpLabel.Label                "Examples: 1/2 or 0.5")
        (HelpLabel.TopOffset            2)
        (HelpLabel.BottomOffset         8)
        (HelpLabel.LeftOffset           12)
        (HelpLabel.RightOffset          12)
        (HelpLabel.AttachLeft           True)
        (HelpLabel.AttachRight          True)
        (FooterBar.TopOffset            4)
        (FooterBar.LeftOffset           6)
        (FooterBar.RightOffset          6)
        (FooterBar.BottomOffset         8)
        (FooterBar.AttachLeft           True)
        (FooterBar.AttachRight          True)
        (FooterBar.AttachBottom         True)
        (.Layout
            (Grid (Rows 1 1 1 1 1 1 1) (Cols 1)
                PromptLabel
                ScopeLabel
                ScopeGroup
                ScaleLabel
                ScaleInput
                HelpLabel
                FooterBar
            )
        )
    )
)

(Layout FooterBar
    (Components
        (PushButton                     CancelBtn)
        (PushButton                     OKBtn)
    )

    (Resources
        (CancelBtn.Label                "Cancel")
        (CancelBtn.TopOffset            4)
        (CancelBtn.BottomOffset         2)
        (OKBtn.Label                    "OK")
        (OKBtn.TopOffset                4)
        (OKBtn.BottomOffset             2)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 1 0 0)
                (Pos 1 2)
                CancelBtn
                (Pos 1 3)
                OKBtn
            )
        )
    )
)

