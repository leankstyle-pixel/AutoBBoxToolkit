(Dialog autobbox_bom_value_edit
    (Components
        (SubLayout                      BomValueEditPage)
    )

    (Resources
        (.Label                         "Edit BOM Value")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                BomValueEditPage
            )
        )
    )
)

(Layout BomValueEditPage
    (Components
        (Label                          PromptLabel)
        (Label                          TypeLabel)
        (InputPanel                     ValueInput)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (PromptLabel.Label              "编辑参数值")
        (TypeLabel.Label                "类型")
        (ValueInput.Columns             48)
        (OKBtn.Label                    "确定")
        (CancelBtn.Label                "取消")
        (.Layout
            (Grid (Rows 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                (Span 1 2)
                TypeLabel
                (Pos 3 1)
                (Span 1 2)
                ValueInput
                (Pos 4 1)
                OKBtn
                (Pos 4 2)
                CancelBtn
            )
        )
    )
)
