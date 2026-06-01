(Dialog autobbox_save_copy_to_workdir
    (Components
        (SubLayout SaveCopyPage)
    )
    (Resources
        (.Label "Assemble Copy")
        (.Resizeable False)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                SaveCopyPage
            )
        )
    )
)

(Layout SaveCopyPage
    (Components
        (Label SourceLabel)
        (InputPanel NewNameInput)
        (CheckButton ReplaceCheck)
        (PushButton OKBtn)
        (PushButton CancelBtn)
    )
    (Resources
        (SourceLabel.Label "Source model:")
        (NewNameInput.Columns 28)
        (NewNameInput.MaxLen 31)
        (ReplaceCheck.Label "Assemble into current assembly after save")
        (OKBtn.Label "Save and Assemble")
        (CancelBtn.Label "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1) (Cols 1 1)
                (Pos 1 1)
                (Span 1 2)
                SourceLabel
                (Pos 2 1)
                (Span 1 2)
                NewNameInput
                (Pos 3 1)
                (Span 1 2)
                ReplaceCheck
                (Pos 4 1)
                OKBtn
                (Pos 4 2)
                CancelBtn
            )
        )
    )
)
