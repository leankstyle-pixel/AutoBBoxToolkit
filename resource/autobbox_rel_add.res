(Dialog autobbox_rel_add
    (Components
        (SubLayout                      RelAddPage)
    )

    (Resources
        (.Label                         "Add Relations")
        (.Resizeable                    True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                RelAddPage
            )
        )
    )
)

(Layout RelAddPage
    (Components
        (Label                          PromptLabel)
        (TextArea                       RelationText)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (PromptLabel.Label              "Paste relations text:")
        (RelationText.Rows              18)
        (RelationText.MinRows           14)
        (RelationText.Columns           110)
        (RelationText.MaxLen            50000)
        (RelationText.Wrap              True)
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                (Span 1 2)
                RelationText
                (Pos 3 1)
                OKBtn
                (Pos 3 2)
                CancelBtn
            )
        )
    )
)
