(Dialog autobbox_param_add
    (Components
        (SubLayout                      ParamAddPage)
    )

    (Resources
        (.Label                         "Add Param")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                ParamAddPage
            )
        )
    )
)

(Layout ParamAddPage
    (Components
        (Label                          PromptLabel)
        (Label                          NameLabel)
        (InputPanel                     NameInput)
        (Label                          TypeLabel)
        (OptionMenu                     TypeMenu)
        (Label                          ValueLabel)
        (InputPanel                     ValueInput)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (PromptLabel.Label              "Create one model parameter")
        (NameLabel.Label                "Name")
        (NameInput.Columns              36)
        (TypeLabel.Label                "Type")
        (TypeMenu.Columns               18)
        (TypeMenu.VisibleRows           4)
        (ValueLabel.Label               "Value")
        (ValueInput.Columns             36)
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                NameLabel
                (Pos 2 2)
                NameInput
                (Pos 3 1)
                TypeLabel
                (Pos 3 2)
                TypeMenu
                (Pos 4 1)
                ValueLabel
                (Pos 4 2)
                ValueInput
                (Pos 5 1)
                OKBtn
                (Pos 5 2)
                CancelBtn
            )
        )
    )
)
