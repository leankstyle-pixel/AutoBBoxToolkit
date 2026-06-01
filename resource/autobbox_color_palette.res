(Dialog autobbox_color_palette
    (Components
        (SubLayout                      PalettePage)
    )

    (Resources
        (.Label                         "Color Palette")
        (.Resizeable                    True)
        (PalettePage.AttachLeft         True)
        (PalettePage.AttachRight        True)
        (PalettePage.AttachTop          True)
        (PalettePage.AttachBottom       True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                PalettePage
            )
        )
    )
)

(Layout PalettePage
    (Components
        (Label                          PromptLabel)
        (Table                          ColorTable)
        (SubLayout                      ButtonBar)
    )

    (Resources
        (PromptLabel.Label              "Select a target color:")
        (PromptLabel.TopOffset          8)
        (PromptLabel.LeftOffset         8)
        (PromptLabel.RightOffset        8)
        (PromptLabel.AttachLeft         True)
        (PromptLabel.AttachRight        True)
        (PromptLabel.AttachTop          True)
        (ColorTable.TopOffset           4)
        (ColorTable.BottomOffset        6)
        (ColorTable.LeftOffset          8)
        (ColorTable.RightOffset         8)
        (ColorTable.AttachLeft          True)
        (ColorTable.AttachRight         True)
        (ColorTable.AttachTop           True)
        (ColorTable.AttachBottom        True)
        (ButtonBar.TopOffset            4)
        (ButtonBar.LeftOffset           8)
        (ButtonBar.RightOffset          8)
        (ButtonBar.BottomOffset         8)
        (ButtonBar.AttachLeft           True)
        (ButtonBar.AttachRight          True)
        (ButtonBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0) (Cols 1)
                (Pos 1 1)
                PromptLabel
                (Pos 2 1)
                ColorTable
                (Pos 3 1)
                ButtonBar
            )
        )
    )
)

(Layout ButtonBar
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

