(Dialog autobbox_ft_add_column
    (Components
        (SubLayout MainPage)
    )
    (Resources
        (.Label "Add Family Table Item")
        (.Resizeable True)
        (.Layout (Grid (Rows 1) (Cols 1) MainPage))
    )
)

(Layout MainPage
    (Components
        (Label PromptLabel)
        (Label TypeLabel)
        (OptionMenu ColumnTypeMenu)
        (Label ObjectLabel)
        (InputPanel ObjectInput)
        (Label InsertLabel)
        (InputPanel InsertInput)
        (Label StatusLabel)
        (Separator Sep1)
        (PushButton OKBtn)
        (PushButton CancelBtn)
    )
    (Resources
        (PromptLabel.Label "Add Item: select native family-table item type, object name/id, and insert position.")
        (PromptLabel.AttachLeft True)
        (PromptLabel.AttachRight True)
        (TypeLabel.Label "Item type:")
        (ObjectLabel.Label "Object name / id:")
        (InsertLabel.Label "Insert index, blank = end:")
        (ColumnTypeMenu.Columns 28)
        (ObjectInput.Columns 42)
        (ObjectInput.MaxLen 128)
        (InsertInput.Columns 12)
        (InsertInput.InputType 2)
        (InsertInput.MaxLen 8)
(StatusLabel.Label "PARAM is safest FULL path. FEATURE/MEMBER/UDF/REF_MODEL/MERGE also accept model-tree/property names. DIM accepts native dim name or feature id/FEAT:id. FEATURE/REF_MODEL/MERGE accept feature id/FEAT:id. MEMBER accepts member/component id or MEMBER:id/ASM:id. UDF accepts udf/group id or UDF:id.")
        (StatusLabel.AttachLeft True)
        (StatusLabel.AttachRight True)
        (Sep1.TopOffset 0)
        (Sep1.BottomOffset 0)
        (OKBtn.Label "OK")
        (CancelBtn.Label "Cancel")
        (.TopOffset 8)
        (.BottomOffset 8)
        (.LeftOffset 8)
        (.RightOffset 8)
        (.Layout
            (Grid (Rows 0 0 0 0 0 0 0 0 0) (Cols 1)
                PromptLabel
                TypeLabel
                ColumnTypeMenu
                ObjectLabel
                ObjectInput
                InsertLabel
                InsertInput
                StatusLabel
                Sep1
                (Grid (Rows 0) (Cols 1 1)
                    OKBtn
                    CancelBtn
                )
            )
        )
    )
)
