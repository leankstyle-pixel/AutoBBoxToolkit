(Dialog autobbox_drawing_arrange
    (Components
        (SubLayout                      DrawingArrangePage)
    )
    (Resources
        (.Label                         "Arrange Drawing Views")
        (.Resizeable                    False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                DrawingArrangePage
            )
        )
    )
)

(Layout DrawingArrangePage
    (Components
        (Label                          PromptLabel)
        (CheckButton                    FrameCheck)
        (CheckButton                    TitleCheck)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )
    (Resources
        (PromptLabel.Label              "Select post-arrange options:")
        (FrameCheck.Label               "Add frame")
        (TitleCheck.Label               "Update model name and quantity")
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                PromptLabel
                (Pos 2 1)
                (Span 1 2)
                FrameCheck
                (Pos 3 1)
                (Span 1 2)
                TitleCheck
                (Pos 4 1)
                OKBtn
                (Pos 4 2)
                CancelBtn
            )
        )
    )
)
