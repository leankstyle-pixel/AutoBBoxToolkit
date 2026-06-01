(Dialog autobbox_drawing_export
    (Components
        (SubLayout                      DrawingExportPage)
    )

    (Resources
        (.Label                         "Drawing Export")
        (.Resizeable                    False)
        (DrawingExportPage.AttachLeft   True)
        (DrawingExportPage.AttachRight  True)
        (DrawingExportPage.AttachTop    True)
        (DrawingExportPage.AttachBottom True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                DrawingExportPage
            )
        )
    )
)

(Layout DrawingExportPage
    (Components
        (Label                          PromptLabel)
        (RadioGroup                     FormatGroup)
        (Label                          DwgModeLabel)
        (RadioGroup                     DwgModeGroup)
        (Label                          SheetLabel)
        (List                           SheetList)
        (Label                          OutputLabel)
        (SubLayout                      FooterBar)
    )

    (Resources
        (PromptLabel.Label              "Select current drawing export format:")
        (PromptLabel.TopOffset          10)
        (PromptLabel.BottomOffset       4)
        (PromptLabel.LeftOffset         10)
        (PromptLabel.RightOffset        10)
        (PromptLabel.AttachLeft         True)
        (FormatGroup.Names              "dwg" "pdf" "dxf")
        (FormatGroup.Labels             "DWG" "PDF" "DXF")
        (FormatGroup.TopOffset          2)
        (FormatGroup.BottomOffset       8)
        (FormatGroup.LeftOffset         12)
        (FormatGroup.RightOffset        12)
        (FormatGroup.AttachLeft         True)
        (DwgModeLabel.Label             "DWG multi-sheet output mode:")
        (DwgModeLabel.TopOffset         4)
        (DwgModeLabel.BottomOffset      2)
        (DwgModeLabel.LeftOffset        10)
        (DwgModeLabel.RightOffset       10)
        (DwgModeLabel.AttachLeft        True)
        (DwgModeGroup.Names             "multi_layout" "per_sheet")
        (DwgModeGroup.Labels            "Multi-layout DWG" "Separate DWG per sheet")
        (DwgModeGroup.TopOffset         2)
        (DwgModeGroup.BottomOffset      8)
        (DwgModeGroup.LeftOffset        12)
        (DwgModeGroup.RightOffset       12)
        (DwgModeGroup.AttachLeft        True)
        (SheetLabel.Label               "Select DWG sheets:")
        (SheetLabel.TopOffset           4)
        (SheetLabel.BottomOffset        2)
        (SheetLabel.LeftOffset          10)
        (SheetLabel.RightOffset         10)
        (SheetLabel.AttachLeft          True)
        (SheetList.VisibleRows          4)
        (SheetList.MinRows              3)
        (SheetList.Columns              1)
        (SheetList.TopOffset            2)
        (SheetList.BottomOffset         8)
        (SheetList.LeftOffset           12)
        (SheetList.RightOffset          12)
        (SheetList.AttachLeft           True)
        (SheetList.AttachRight          True)
        (OutputLabel.Label              "Output to current working directory export folder.")
        (OutputLabel.TopOffset          4)
        (OutputLabel.BottomOffset       8)
        (OutputLabel.LeftOffset         10)
        (OutputLabel.RightOffset        10)
        (OutputLabel.AttachLeft         True)
        (OutputLabel.AttachRight        True)
        (FooterBar.TopOffset            4)
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
            (Grid (Rows 0 0 0 0 0 0 0 0) (Cols 1)
                PromptLabel
                FormatGroup
                DwgModeLabel
                DwgModeGroup
                SheetLabel
                SheetList
                OutputLabel
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
        (OKBtn.Label                    "Export")
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

