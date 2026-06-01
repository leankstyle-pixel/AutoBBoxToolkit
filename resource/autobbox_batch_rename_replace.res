(Dialog autobbox_batch_rename_replace
    (Components
        (SubLayout                      ReplacePage)
    )
    (Resources
        (.Label                         "Replace")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                ReplacePage
            )
        )
    )
)

(Layout ReplacePage
    (Components
        (Label                          PromptLabel)
        (Label                          TargetLabel)
        (OptionMenu                     TargetMenu)
        (Label                          ModeLabel)
        (OptionMenu                     ModeMenu)
        (Label                          CaseLabel)
        (OptionMenu                     CaseMenu)
        (Label                          FindLabel)
        (InputPanel                     FindInput)
        (Label                          PresetLabel)
        (OptionMenu                     PresetMenu)
        (Label                          ReplaceLabel)
        (InputPanel                     ReplaceInput)
        (Label                          HelpLabel)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )
    (Resources
        (PromptLabel.Label              "Replace in batch")
        (TargetLabel.Label              "Target")
        (TargetMenu.Columns             22)
        (TargetMenu.VisibleRows         2)
        (ModeLabel.Label                "Mode")
        (ModeMenu.Columns               18)
        (ModeMenu.VisibleRows           2)
        (CaseLabel.Label                "Case")
        (CaseMenu.Columns               18)
        (CaseMenu.VisibleRows           2)
        (FindLabel.Label                "Find")
        (FindInput.Columns              42)
        (PresetLabel.Label              "Template options")
        (PresetMenu.Columns             26)
        (PresetMenu.VisibleRows         5)
        (ReplaceLabel.Label             "Replace")
        (ReplaceInput.Columns           42)
        (HelpLabel.Label                "Tokens: {model} {name} {target} {common} {num} {row} {match}; model parameter: {PARAM_NAME}")
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1 1 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                TargetLabel
                (Pos 2 2)
                TargetMenu
                (Pos 3 1)
                ModeLabel
                (Pos 3 2)
                ModeMenu
                (Pos 4 1)
                CaseLabel
                (Pos 4 2)
                CaseMenu
                (Pos 5 1)
                FindLabel
                (Pos 5 2)
                FindInput
                (Pos 6 1)
                PresetLabel
                (Pos 6 2)
                PresetMenu
                (Pos 7 1)
                ReplaceLabel
                (Pos 7 2)
                ReplaceInput
                (Pos 8 1)
                (Span 1 2)
                HelpLabel
                (Pos 9 1)
                OKBtn
                (Pos 9 2)
                CancelBtn
            )
        )
    )
)
