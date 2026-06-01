(Dialog autobbox_split_pick
    (Components
        (SubLayout                      SplitPickPage)
    )

    (Resources
        (.Label                         "Split Instances")
        (.Layout
            (Grid (Rows 1) (Cols 1)
                SplitPickPage
            )
        )
    )
)

(Layout SplitPickPage
    (Components
        (Label                          PromptLabel)
        (List                           ModelList)
        (CheckButton                    ReplaceCheck)
        (CheckButton                    OutDirCheck)
        (CheckButton                    ReuseCheck)
        (PushButton                     SelectAllBtn)
        (PushButton                     ClearBtn)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (PromptLabel.Label              "Select instance models to split:")
        (ModelList.VisibleRows          10)
        (ModelList.MinRows              6)
        (ModelList.Columns              1)
        (ReplaceCheck.Label             "Replace current assembly reference")
        (OutDirCheck.Label              "Output to AB_SPLIT folder")
        (ReuseCheck.Label               "Reuse existing _SPLIT model")
        (SelectAllBtn.Label             "Select All")
        (ClearBtn.Label                 "Clear")
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1 1 1 1) (Cols 1 1 1 1)
                (Pos 1 1)
                (Span 1 4)
                PromptLabel
                (Pos 2 1)
                (Span 1 4)
                ModelList
                (Pos 3 1)
                (Span 1 4)
                ReplaceCheck
                (Pos 4 1)
                (Span 1 4)
                OutDirCheck
                (Pos 5 1)
                (Span 1 4)
                ReuseCheck
                (Pos 6 1)
                SelectAllBtn
                (Pos 6 2)
                ClearBtn
                (Pos 6 3)
                OKBtn
                (Pos 6 4)
                CancelBtn
            )
        )
    )
)
