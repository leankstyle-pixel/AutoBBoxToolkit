(Dialog autobbox_batch_rename_clear
    (Components
        (SubLayout                      ClearPage)
    )
    (Resources
        (.Label                         "Clear")
        (.Resizeable                    False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                ClearPage
            )
        )
    )
)

(Layout ClearPage
    (Components
        (Label                          PromptLabel)
        (Label                          TargetLabel)
        (OptionMenu                     TargetMenu)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )
    (Resources
        (PromptLabel.Label              "Select column")
        (TargetLabel.Label              "Target")
        (TargetMenu.Columns             18)
        (TargetMenu.VisibleRows         3)
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                TargetLabel
                (Pos 2 2)
                TargetMenu
                (Pos 3 1)
                OKBtn
                (Pos 3 2)
                CancelBtn
            )
        )
    )
)
