(Dialog autobbox_delete_opts
    (Components
        (SubLayout                      DeleteOptionsPage)
    )

    (Resources
        (.Label                         "Delete Params")
        (.Layout
            (Grid (Rows 1) (Cols 1)
                DeleteOptionsPage
            )
        )
    )
)

(Layout DeleteOptionsPage
    (Components
        (CheckButton                    DelSizeCheck)
        (CheckButton                    DelVolCheck)
        (PushButton                     OKBtn)
        (PushButton                     CancelBtn)
    )

    (Resources
        (DelSizeCheck.Label             "Delete Size (BBOX_LXWXH)")
        (DelVolCheck.Label              "Delete Volume (BBOX_VOL_M3)")
        (OKBtn.Label                    "OK")
        (CancelBtn.Label                "Cancel")
        (.Layout
            (Grid (Rows 1 1 1) (Cols 1 1)
                (Pos 1 1)
                DelSizeCheck
                (Pos 2 1)
                DelVolCheck
                (Pos 3 1)
                OKBtn
                (Pos 3 2)
                CancelBtn
            )
        )
    )
)
