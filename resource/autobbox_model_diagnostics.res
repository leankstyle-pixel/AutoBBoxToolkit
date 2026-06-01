(Dialog autobbox_model_diagnostics
    (Components
        (SubLayout                      ModelDiagnosticsPage)
    )

    (Resources
        (.Label                         "Model Diagnostics")
        (.Resizeable                    True)
        (ModelDiagnosticsPage.AttachLeft True)
        (ModelDiagnosticsPage.AttachRight True)
        (ModelDiagnosticsPage.AttachTop True)
        (ModelDiagnosticsPage.AttachBottom True)
        (.Layout
            (Grid (Rows 1) (Cols 1)
                ModelDiagnosticsPage
            )
        )
    )
)

(Layout ModelDiagnosticsPage
    (Components
        (Label                          SummaryLabel)
        (Table                          IssueTable)
        (Label                          StatusLabel)
        (SubLayout                      ButtonBar)
    )

    (Resources
        (SummaryLabel.Label             "Model diagnostics summary")
        (SummaryLabel.TopOffset         8)
        (SummaryLabel.BottomOffset      4)
        (SummaryLabel.LeftOffset        8)
        (SummaryLabel.RightOffset       8)
        (SummaryLabel.AttachLeft        True)
        (SummaryLabel.AttachRight       True)
        (SummaryLabel.AttachTop         True)
        (IssueTable.TopOffset           4)
        (IssueTable.BottomOffset        4)
        (IssueTable.LeftOffset          8)
        (IssueTable.RightOffset         8)
        (IssueTable.AttachLeft          True)
        (IssueTable.AttachRight         True)
        (IssueTable.AttachTop           True)
        (IssueTable.AttachBottom        True)
        (StatusLabel.Label              "Select an issue row.")
        (StatusLabel.TopOffset          4)
        (StatusLabel.BottomOffset       4)
        (StatusLabel.LeftOffset         8)
        (StatusLabel.RightOffset        8)
        (StatusLabel.AttachLeft         True)
        (StatusLabel.AttachRight        True)
        (ButtonBar.TopOffset            4)
        (ButtonBar.BottomOffset         8)
        (ButtonBar.LeftOffset           8)
        (ButtonBar.RightOffset          8)
        (ButtonBar.AttachLeft           True)
        (ButtonBar.AttachRight          True)
        (ButtonBar.AttachBottom         True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachTop                     True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 0 1 0 0) (Cols 1)
                (Pos 1 1)
                SummaryLabel
                (Pos 2 1)
                IssueTable
                (Pos 3 1)
                StatusLabel
                (Pos 4 1)
                ButtonBar
            )
        )
    )
)

(Layout ButtonBar
    (Components
        (PushButton                     LocateBtn)
        (PushButton                     DeepBtn)
        (PushButton                     ReportBtn)
        (PushButton                     CloseBtn)
    )

    (Resources
        (LocateBtn.Label                "Locate Model")
        (LocateBtn.TopOffset            4)
        (LocateBtn.BottomOffset         4)
        (LocateBtn.LeftOffset           4)
        (DeepBtn.Label                  "Deep Check")
        (DeepBtn.TopOffset              4)
        (DeepBtn.BottomOffset           4)
        (ReportBtn.Label                "Open Report")
        (ReportBtn.TopOffset            4)
        (ReportBtn.BottomOffset         4)
        (CloseBtn.Label                 "Close")
        (CloseBtn.TopOffset             4)
        (CloseBtn.BottomOffset          4)
        (CloseBtn.RightOffset           4)
        (CloseBtn.AttachRight           True)
        (.AttachLeft                    True)
        (.AttachRight                   True)
        (.AttachBottom                  True)
        (.Layout
            (Grid (Rows 1) (Cols 0 0 0 1 0)
                (Pos 1 1)
                LocateBtn
                (Pos 1 2)
                DeepBtn
                (Pos 1 3)
                ReportBtn
                (Pos 1 5)
                CloseBtn
            )
        )
    )
)
