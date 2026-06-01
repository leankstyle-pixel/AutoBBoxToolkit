(Dialog autobbox_drawing_view_brush
    (Components
        (SubLayout                      BrushPage)
    )

    (Resources
        (.Label                         "Drawing View Brush")
        (.Resizeable                    False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                BrushPage
            )
        )
    )
)

(Layout BrushPage
    (Components
        (Label                          ModeLabel)
        (RadioGroup                     ModeGroup)
        (Label                          PromptLabel)
        (RadioGroup                     SourceGroup)
        (Label                          PresetLabel)
        (OptionMenu                     PresetMenu)
        (SubLayout                      FooterBar)
    )

    (Resources
        (ModeLabel.Label                "Brush mode:")
        (ModeLabel.TopOffset            12)
        (ModeLabel.BottomOffset         4)
        (ModeLabel.LeftOffset           10)
        (ModeLabel.RightOffset          6)
        (ModeLabel.AttachLeft           True)
        (ModeGroup.Names                "main" "axon")
        (ModeGroup.Labels               "Main view mode" "Axonometric view mode")
        (ModeGroup.TopOffset            8)
        (ModeGroup.BottomOffset         8)
        (ModeGroup.LeftOffset           12)
        (ModeGroup.RightOffset          10)
        (ModeGroup.AttachLeft           True)
        (PromptLabel.Label              "Select view orientation source:")
        (PromptLabel.TopOffset          8)
        (PromptLabel.BottomOffset       4)
        (PromptLabel.LeftOffset         10)
        (PromptLabel.RightOffset        6)
        (PromptLabel.AttachLeft         True)
        (SourceGroup.Names              "reference" "manual")
        (SourceGroup.Labels             "Reference view" "Manual direction")
        (SourceGroup.TopOffset          2)
        (SourceGroup.BottomOffset       8)
        (SourceGroup.LeftOffset         12)
        (SourceGroup.RightOffset        12)
        (SourceGroup.AttachLeft         True)
        (PresetLabel.Label              "Manual direction:")
        (PresetLabel.TopOffset          8)
        (PresetLabel.BottomOffset       2)
        (PresetLabel.LeftOffset         10)
        (PresetLabel.RightOffset        6)
        (PresetLabel.AttachLeft         True)
        (PresetMenu.TopOffset           4)
        (PresetMenu.BottomOffset        10)
        (PresetMenu.LeftOffset          12)
        (PresetMenu.RightOffset         10)
        (PresetMenu.AttachLeft          True)
        (PresetMenu.AttachRight         True)
        (FooterBar.TopOffset            8)
        (FooterBar.LeftOffset           6)
        (FooterBar.RightOffset          6)
        (FooterBar.BottomOffset         8)
        (FooterBar.AttachLeft           True)
        (FooterBar.AttachRight          True)
        (FooterBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 0 0 0) (Cols 0 1)
                (Pos 1 1)
                ModeLabel
                (Pos 1 2)
                ModeGroup
                (Pos 2 1)
                PromptLabel
                (Pos 2 2)
                SourceGroup
                (Pos 3 1)
                PresetLabel
                (Pos 3 2)
                PresetMenu
                (Pos 4 1)
                (Span 1 2)
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
