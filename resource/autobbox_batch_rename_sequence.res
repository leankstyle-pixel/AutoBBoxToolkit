(Dialog autobbox_batch_rename_sequence
    (Components
        (SubLayout                      SequencePage)
    )
    (Resources
        (.Label                         "Sequence")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                SequencePage
            )
        )
    )
)

(Layout SequencePage
    (Components
        (Label                          PromptLabel)
        (Label                          TargetLabel)
        (OptionMenu                     TargetMenu)
        (Label                          PresetLabel)
        (OptionMenu                     PresetMenu)
        (Label                          TemplateLabel)
        (InputPanel                     TemplateInput)
        (Label                          HelpLabel)
        (Label                          StartLabel)
        (InputPanel                     StartInput)
        (Label                          StepLabel)
        (InputPanel                     StepInput)
        (Label                          WidthLabel)
        (InputPanel                     WidthInput)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )
    (Resources
        (PromptLabel.Label              "Generate sequence")
        (TargetLabel.Label              "Target")
        (TargetMenu.Columns             22)
        (TargetMenu.VisibleRows         2)
        (PresetLabel.Label              "Template options")
        (PresetMenu.Columns             26)
        (PresetMenu.VisibleRows         5)
        (TemplateLabel.Label            "Template")
        (TemplateInput.Columns          42)
        (HelpLabel.Label                "Tokens: {model} {name} {target} {common} {num} {row}; model parameter: {PARAM_NAME}")
        (StartLabel.Label               "Start")
        (StartInput.Columns             12)
        (StepLabel.Label                "Step")
        (StepInput.Columns              12)
        (WidthLabel.Label               "Width")
        (WidthInput.Columns             12)
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
                PresetLabel
                (Pos 3 2)
                PresetMenu
                (Pos 4 1)
                TemplateLabel
                (Pos 4 2)
                TemplateInput
                (Pos 5 1)
                (Span 1 2)
                HelpLabel
                (Pos 6 1)
                StartLabel
                (Pos 6 2)
                StartInput
                (Pos 7 1)
                StepLabel
                (Pos 7 2)
                StepInput
                (Pos 8 1)
                WidthLabel
                (Pos 8 2)
                WidthInput
                (Pos 9 1)
                OKBtn
                (Pos 9 2)
                CancelBtn
            )
        )
    )
)
