$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

function New-Point {
    param(
        [float]$X,
        [float]$Y
    )

    return New-Object System.Drawing.PointF($X, $Y)
}

function New-RoundedRectPath {
    param(
        [System.Drawing.RectangleF]$Rect,
        [float]$Radius
    )

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $diameter = $Radius * 2.0

    if ($diameter -le 0.0) {
        $path.AddRectangle($Rect)
        return $path
    }

    $arc = New-Object System.Drawing.RectangleF($Rect.X, $Rect.Y, $diameter, $diameter)
    $path.AddArc($arc, 180, 90)
    $arc.X = $Rect.Right - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $Rect.Bottom - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $Rect.X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-Rect {
    param(
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height,
        [float]$Scale
    )

    return New-Object System.Drawing.RectangleF(
        [float]($X * $Scale),
        [float]($Y * $Scale),
        [float]($Width * $Scale),
        [float]($Height * $Scale)
    )
}

function New-Color {
    param(
        [int]$R,
        [int]$G,
        [int]$B,
        [int]$A = 255
    )

    return [System.Drawing.Color]::FromArgb($A, $R, $G, $B)
}

function New-ScaledPoints {
    param(
        [float]$Scale,
        [object[]]$Pairs
    )

    $points = New-Object 'System.Collections.Generic.List[System.Drawing.PointF]'
    foreach ($pair in $Pairs) {
        $points.Add((New-Point ([float]$pair[0] * $Scale) ([float]$pair[1] * $Scale)))
    }
    return [System.Drawing.PointF[]]$points.ToArray()
}

function Fill-Polygon {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [float]$Scale,
        [object[]]$Pairs
    )

    $Graphics.FillPolygon($Brush, (New-ScaledPoints -Scale $Scale -Pairs $Pairs))
}

function Draw-Polygon {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Pen]$Pen,
        [float]$Scale,
        [object[]]$Pairs
    )

    $Graphics.DrawPolygon($Pen, (New-ScaledPoints -Scale $Scale -Pairs $Pairs))
}

function Draw-Polyline {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Pen]$Pen,
        [float]$Scale,
        [object[]]$Pairs
    )

    $Graphics.DrawLines($Pen, (New-ScaledPoints -Scale $Scale -Pairs $Pairs))
}

function Draw-Line {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Pen]$Pen,
        [float]$Scale,
        [float]$X1,
        [float]$Y1,
        [float]$X2,
        [float]$Y2
    )

    $Graphics.DrawLine(
        $Pen,
        [float]($X1 * $Scale),
        [float]($Y1 * $Scale),
        [float]($X2 * $Scale),
        [float]($Y2 * $Scale)
    )
}

function Draw-DimensionLine {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Color]$Color,
        [float]$Scale,
        [float]$X1,
        [float]$Y1,
        [float]$X2,
        [float]$Y2
    )

    $pen = New-Object System.Drawing.Pen($Color, [float](1.8 * $Scale))
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    Draw-Line -Graphics $Graphics -Pen $pen -Scale $Scale -X1 $X1 -Y1 $Y1 -X2 $X2 -Y2 $Y2

    $arrow = New-Object System.Drawing.SolidBrush($Color)
    if ([math]::Abs($Y1 - $Y2) -lt 0.01) {
        Fill-Polygon -Graphics $Graphics -Brush $arrow -Scale $Scale -Pairs @(
            @([float]$X1, [float]$Y1),
            @([float]($X1 + 1.5), [float]($Y1 - 1.2)),
            @([float]($X1 + 1.5), [float]($Y1 + 1.2))
        )
        Fill-Polygon -Graphics $Graphics -Brush $arrow -Scale $Scale -Pairs @(
            @([float]$X2, [float]$Y2),
            @([float]($X2 - 1.5), [float]($Y2 - 1.2)),
            @([float]($X2 - 1.5), [float]($Y2 + 1.2))
        )
    } else {
        Fill-Polygon -Graphics $Graphics -Brush $arrow -Scale $Scale -Pairs @(
            @([float]$X1, [float]$Y1),
            @([float]($X1 - 1.2), [float]($Y1 + 1.5)),
            @([float]($X1 + 1.2), [float]($Y1 + 1.5))
        )
        Fill-Polygon -Graphics $Graphics -Brush $arrow -Scale $Scale -Pairs @(
            @([float]$X2, [float]$Y2),
            @([float]($X2 - 1.2), [float]($Y2 - 1.5)),
            @([float]($X2 + 1.2), [float]($Y2 - 1.5))
        )
    }

    $arrow.Dispose()
    $pen.Dispose()
}

function Draw-Tile {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$Size,
        [System.Drawing.Color]$FillTop,
        [System.Drawing.Color]$FillBottom
    )

    return ($Size / 32.0)
}

function Draw-Card {
    param(
        [System.Drawing.Graphics]$Graphics,
        [float]$Scale,
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height
    )

    $rect = New-Rect -X $X -Y $Y -Width $Width -Height $Height -Scale $Scale
    $path = New-RoundedRectPath -Rect $rect -Radius (2.6 * $Scale)
    $pen = New-Object System.Drawing.Pen((New-Color 52 70 89), [float](1.6 * $Scale))
    $Graphics.DrawPath($pen, $path)
    $pen.Dispose()
    $path.Dispose()
}

function Draw-TableRows {
    param(
        [System.Drawing.Graphics]$Graphics,
        [float]$Scale,
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float[]]$Rows,
        [float[]]$Cols
    )

    $gridPen = New-Object System.Drawing.Pen((New-Color 132 145 159), [float](1.5 * $Scale))
    foreach ($row in $Rows) {
        Draw-Line -Graphics $Graphics -Pen $gridPen -Scale $Scale -X1 $X -Y1 $row -X2 ($X + $Width) -Y2 $row
    }
    foreach ($col in $Cols) {
        Draw-Line -Graphics $Graphics -Pen $gridPen -Scale $Scale -X1 $col -Y1 $Y -X2 $col -Y2 ($Y + 11.5)
    }
    $gridPen.Dispose()
}

function Draw-IconGlyph {
    param(
        [System.Drawing.Graphics]$Graphics,
        [string]$Name,
        [int]$Size
    )

    $scale = $Size / 32.0
    $fg = New-Color 52 70 89
    $fgSoft = New-Color 132 145 159
    $accentWarm = $fgSoft

    switch ($Name) {
        'autobbox_size' {
            $boxPen = New-Object System.Drawing.Pen($fg, [float](2.2 * $scale))
            $boxPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            $Graphics.DrawRectangle($boxPen, [float](8.5 * $scale), [float](10.0 * $scale), [float](9.5 * $scale), [float](9.0 * $scale))
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 8.5 -Y1 10.0 -X2 7.0 -Y2 10.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 18.0 -Y1 10.0 -X2 19.5 -Y2 10.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 18.0 -Y1 19.0 -X2 19.5 -Y2 19.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 8.5 -Y1 19.0 -X2 7.0 -Y2 19.0
            Draw-DimensionLine -Graphics $Graphics -Color $fg -Scale $scale -X1 6.4 -Y1 7.0 -X2 19.6 -Y2 7.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 8.5 -Y1 8.0 -X2 8.5 -Y2 10.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 18.0 -Y1 8.0 -X2 18.0 -Y2 10.0
            Draw-DimensionLine -Graphics $Graphics -Color $fg -Scale $scale -X1 23.5 -Y1 9.0 -X2 23.5 -Y2 20.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 21.7 -Y1 10.0 -X2 19.8 -Y2 10.0
            Draw-Line -Graphics $Graphics -Pen $boxPen -Scale $scale -X1 21.7 -Y1 19.0 -X2 19.8 -Y2 19.0
            $boxPen.Dispose()
        }
        'autobbox_volume' {
            $pen = New-Object System.Drawing.Pen($fg, [float](2.1 * $scale))
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(9.0, 11.5), @(17.2, 8.0), @(23.0, 12.0), @(14.8, 15.7)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(9.0, 11.5), @(9.0, 19.8), @(14.8, 23.8), @(14.8, 15.7)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(14.8, 15.7), @(23.0, 12.0), @(23.0, 20.1), @(14.8, 23.8)
            )
            Draw-Line -Graphics $Graphics -Pen $pen -Scale $scale -X1 17.2 -Y1 8.0 -X2 17.2 -Y2 16.1
            Draw-Line -Graphics $Graphics -Pen $pen -Scale $scale -X1 17.2 -Y1 16.1 -X2 9.0 -Y2 19.8
            $pen.Dispose()
        }
        'autobbox_iso' {
            Draw-Card -Graphics $Graphics -Scale $scale -X 6.4 -Y 7.0 -Width 19.2 -Height 18.3
            $pen = New-Object System.Drawing.Pen($fg, [float](1.9 * $scale))
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(11.0, 13.0), @(15.6, 11.0), @(19.4, 13.4), @(14.8, 15.3)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(11.0, 13.0), @(11.0, 18.3), @(14.8, 20.7), @(14.8, 15.3)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(14.8, 15.3), @(19.4, 13.4), @(19.4, 18.6), @(14.8, 20.7)
            )
            $arrowPen = New-Object System.Drawing.Pen($fg, [float](1.8 * $scale))
            $arrowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $arrowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Polyline -Graphics $Graphics -Pen $arrowPen -Scale $scale -Pairs @(
                @(20.3, 10.0), @(24.2, 10.0), @(24.2, 14.2)
            )
            $arrowBrush = New-Object System.Drawing.SolidBrush($fg)
            Fill-Polygon -Graphics $Graphics -Brush $arrowBrush -Scale $scale -Pairs @(
                @(24.2, 14.2), @(22.2, 13.7), @(23.7, 12.2)
            )
            $arrowBrush.Dispose()
            $arrowPen.Dispose()
            $pen.Dispose()
        }
        'autobbox_dwg3' {
            Draw-Card -Graphics $Graphics -Scale $scale -X 5.8 -Y 6.2 -Width 20.4 -Height 19.6
            $pen = New-Object System.Drawing.Pen($fg, [float](1.9 * $scale))
            $Graphics.DrawRectangle($pen, [float](10.0 * $scale), [float](14.0 * $scale), [float](6.0 * $scale), [float](5.5 * $scale))
            $Graphics.DrawRectangle($pen, [float](10.0 * $scale), [float](8.0 * $scale), [float](6.0 * $scale), [float](4.0 * $scale))
            $Graphics.DrawRectangle($pen, [float](17.8 * $scale), [float](14.0 * $scale), [float](4.5 * $scale), [float](5.5 * $scale))
            $pen.Dispose()
        }
        'autobbox_scale_sync' {
            Draw-Card -Graphics $Graphics -Scale $scale -X 5.7 -Y 6.0 -Width 20.6 -Height 19.8
            $pagePen = New-Object System.Drawing.Pen($fg, [float](1.8 * $scale))
            $viewPen = New-Object System.Drawing.Pen($fgSoft, [float](1.4 * $scale))
            $arrowPen = New-Object System.Drawing.Pen($fg, [float](1.9 * $scale))
            $arrowPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $arrowPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

            Draw-DimensionLine -Graphics $Graphics -Color $fg -Scale $scale -X1 8.2 -Y1 10.0 -X2 23.8 -Y2 10.0
            $Graphics.DrawRectangle($viewPen, [float](9.2 * $scale), [float](14.2 * $scale), [float](4.8 * $scale), [float](4.4 * $scale))
            $Graphics.DrawRectangle($viewPen, [float](15.7 * $scale), [float](14.2 * $scale), [float](4.8 * $scale), [float](4.4 * $scale))
            $Graphics.DrawRectangle($viewPen, [float](12.5 * $scale), [float](19.3 * $scale), [float](4.8 * $scale), [float](4.0 * $scale))
            Draw-Line -Graphics $Graphics -Pen $arrowPen -Scale $scale -X1 16.0 -Y1 12.0 -X2 16.0 -Y2 17.6
            Fill-Polygon -Graphics $Graphics -Brush (New-Object System.Drawing.SolidBrush($fg)) -Scale $scale -Pairs @(
                @(16.0, 18.8), @(14.5, 16.6), @(17.5, 16.6)
            )

            $arrowPen.Dispose()
            $viewPen.Dispose()
            $pagePen.Dispose()
        }
        'autobbox_param_tool' {
            Draw-Card -Graphics $Graphics -Scale $scale -X 5.5 -Y 6.0 -Width 15.0 -Height 18.0
            Draw-TableRows -Graphics $Graphics -Scale $scale -X 7.2 -Y 9.0 -Width 11.6 -Rows @(13.0, 17.0) -Cols @(10.2, 15.0)

            $pencilBody = New-Object System.Drawing.Pen($fg, [float](3.6 * $scale))
            $pencilBody.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pencilBody.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $pencilBody -Scale $scale -X1 18.5 -Y1 21.8 -X2 26.4 -Y2 13.8

            $pencilLead = New-Object System.Drawing.Pen($fg, [float](2.0 * $scale))
            $pencilLead.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pencilLead.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $pencilLead -Scale $scale -X1 24.8 -Y1 15.4 -X2 27.0 -Y2 13.2

            $eraser = New-Object System.Drawing.Pen($fgSoft, [float](2.4 * $scale))
            $eraser.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $eraser.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $eraser -Scale $scale -X1 17.1 -Y1 23.1 -X2 18.8 -Y2 21.4

            $eraser.Dispose()
            $pencilLead.Dispose()
            $pencilBody.Dispose()
        }
        'autobbox_delete' {
            Draw-Card -Graphics $Graphics -Scale $scale -X 7.0 -Y 7.0 -Width 14.0 -Height 17.0
            $linePen = New-Object System.Drawing.Pen($fg, [float](1.5 * $scale))
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 10.0 -Y1 12.0 -X2 18.0 -Y2 12.0
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 10.0 -Y1 15.5 -X2 18.0 -Y2 15.5
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 10.0 -Y1 19.0 -X2 15.8 -Y2 19.0
            $badgePen = New-Object System.Drawing.Pen($fgSoft, [float](1.7 * $scale))
            $Graphics.DrawEllipse($badgePen, [float](17.3 * $scale), [float](17.0 * $scale), [float](8.2 * $scale), [float](8.2 * $scale))
            $minusPen = New-Object System.Drawing.Pen($fgSoft, [float](2.1 * $scale))
            Draw-Line -Graphics $Graphics -Pen $minusPen -Scale $scale -X1 19.3 -Y1 21.1 -X2 23.6 -Y2 21.1
            $minusPen.Dispose()
            $badgePen.Dispose()
            $linePen.Dispose()
        }
        'autobbox_split' {
            $nodeBrush = New-Object System.Drawing.SolidBrush($fg)
            $linePen = New-Object System.Drawing.Pen($fg, [float](2.2 * $scale))
            $linePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $linePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

            $Graphics.FillRectangle($nodeBrush, [float](6.5 * $scale), [float](12.5 * $scale), [float](6.2 * $scale), [float](6.2 * $scale))
            $Graphics.FillRectangle($nodeBrush, [float](19.3 * $scale), [float](7.0 * $scale), [float](6.2 * $scale), [float](6.2 * $scale))
            $Graphics.FillRectangle($nodeBrush, [float](19.3 * $scale), [float](18.8 * $scale), [float](6.2 * $scale), [float](6.2 * $scale))
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 12.7 -Y1 15.6 -X2 17.0 -Y2 15.6
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 17.0 -Y1 15.6 -X2 17.0 -Y2 10.1
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 17.0 -Y1 15.6 -X2 17.0 -Y2 21.9
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 17.0 -Y1 10.1 -X2 19.3 -Y2 10.1
            Draw-Line -Graphics $Graphics -Pen $linePen -Scale $scale -X1 17.0 -Y1 21.9 -X2 19.3 -Y2 21.9

            $linePen.Dispose()
            $nodeBrush.Dispose()
        }
        'autobbox_rel_clean' {
            $pen = New-Object System.Drawing.Pen($fg, [float](2.0 * $scale))
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            $Graphics.DrawArc($pen, [float](8.5 * $scale), [float](9.0 * $scale), [float](9.0 * $scale), [float](9.0 * $scale), 300, 240)
            $Graphics.DrawArc($pen, [float](14.5 * $scale), [float](14.0 * $scale), [float](9.0 * $scale), [float](9.0 * $scale), 120, 240)
            $broomPen = New-Object System.Drawing.Pen($accentWarm, [float](2.4 * $scale))
            $broomPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $broomPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $broomPen -Scale $scale -X1 19.0 -Y1 8.5 -X2 25.0 -Y2 14.5
            $broomBrush = New-Object System.Drawing.SolidBrush($accentWarm)
            Fill-Polygon -Graphics $Graphics -Brush $broomBrush -Scale $scale -Pairs @(
                @(16.8, 8.4), @(20.7, 7.4), @(22.0, 10.2), @(18.1, 11.1)
            )
            $broomBrush.Dispose()
            $broomPen.Dispose()
            $pen.Dispose()
        }
        'autobbox_rel_add' {
            $pen = New-Object System.Drawing.Pen($fg, [float](2.0 * $scale))
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            $Graphics.DrawArc($pen, [float](8.5 * $scale), [float](10.0 * $scale), [float](8.5 * $scale), [float](8.5 * $scale), 300, 240)
            $Graphics.DrawArc($pen, [float](14.0 * $scale), [float](13.5 * $scale), [float](8.5 * $scale), [float](8.5 * $scale), 120, 240)
            $plusPen = New-Object System.Drawing.Pen($accentWarm, [float](2.3 * $scale))
            $plusPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $plusPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $plusPen -Scale $scale -X1 23.0 -Y1 9.7 -X2 23.0 -Y2 16.0
            Draw-Line -Graphics $Graphics -Pen $plusPen -Scale $scale -X1 19.9 -Y1 12.8 -X2 26.2 -Y2 12.8
            $plusPen.Dispose()
            $pen.Dispose()
        }
        'autobbox_parts' {
            $pen = New-Object System.Drawing.Pen($fg, [float](2.0 * $scale))
            $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(9.0, 11.7), @(16.1, 8.4), @(22.0, 12.0), @(14.8, 15.1)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(9.0, 11.7), @(9.0, 19.6), @(14.8, 23.4), @(14.8, 15.1)
            )
            Draw-Polygon -Graphics $Graphics -Pen $pen -Scale $scale -Pairs @(
                @(14.8, 15.1), @(22.0, 12.0), @(22.0, 19.8), @(14.8, 23.4)
            )
            $pen.Dispose()
        }
        'autobbox_asm' {
            $brush = New-Object System.Drawing.SolidBrush($fg)
            Fill-Polygon -Graphics $Graphics -Brush $brush -Scale $scale -Pairs @(
                @(7.2, 11.5), @(11.0, 9.6), @(14.7, 11.7), @(11.0, 13.7)
            )
            Fill-Polygon -Graphics $Graphics -Brush $brush -Scale $scale -Pairs @(
                @(17.3, 11.5), @(21.0, 9.6), @(24.8, 11.7), @(21.0, 13.7)
            )
            Fill-Polygon -Graphics $Graphics -Brush $brush -Scale $scale -Pairs @(
                @(12.2, 18.0), @(16.0, 16.0), @(19.8, 18.0), @(16.0, 20.1)
            )
            $linkPen = New-Object System.Drawing.Pen($fgSoft, [float](1.9 * $scale))
            $linkPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $linkPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            Draw-Line -Graphics $Graphics -Pen $linkPen -Scale $scale -X1 11.0 -Y1 13.5 -X2 14.0 -Y2 17.0
            Draw-Line -Graphics $Graphics -Pen $linkPen -Scale $scale -X1 21.0 -Y1 13.5 -X2 18.0 -Y2 17.0
            $linkPen.Dispose()
            $brush.Dispose()
        }
        'autobbox_surface' {
            $surfaceBrush = New-Object System.Drawing.SolidBrush($fgSoft)
            Fill-Polygon -Graphics $Graphics -Brush $surfaceBrush -Scale $scale -Pairs @(
                @(8.0, 18.0), @(12.8, 9.8), @(23.4, 11.6), @(18.8, 21.2)
            )
            $linePen = New-Object System.Drawing.Pen($fg, [float](1.9 * $scale))
            Draw-Polyline -Graphics $Graphics -Pen $linePen -Scale $scale -Pairs @(
                @(8.7, 17.5), @(12.5, 12.5), @(18.3, 13.0), @(22.5, 16.0)
            )
            Draw-Polyline -Graphics $Graphics -Pen $linePen -Scale $scale -Pairs @(
                @(10.5, 20.0), @(14.0, 14.8), @(19.5, 15.6), @(20.4, 18.6)
            )
            $edgePen = New-Object System.Drawing.Pen($fg, [float](1.7 * $scale))
            Draw-Polygon -Graphics $Graphics -Pen $edgePen -Scale $scale -Pairs @(
                @(8.0, 18.0), @(12.8, 9.8), @(23.4, 11.6), @(18.8, 21.2)
            )
            $edgePen.Dispose()
            $linePen.Dispose()
            $surfaceBrush.Dispose()
        }
        'autobbox_curve' {
            $nodeBrush = New-Object System.Drawing.SolidBrush($fg)
            $curvePen = New-Object System.Drawing.Pen($fg, [float](2.2 * $scale))
            $curvePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $curvePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $Graphics.DrawBezier(
                $curvePen,
                (New-Point (8.0 * $scale) (20.5 * $scale)),
                (New-Point (11.5 * $scale) (8.0 * $scale)),
                (New-Point (20.2 * $scale) (25.0 * $scale)),
                (New-Point (24.0 * $scale) (11.0 * $scale))
            )
            $Graphics.FillEllipse($nodeBrush, [float](6.4 * $scale), [float](18.9 * $scale), [float](3.3 * $scale), [float](3.3 * $scale))
            $Graphics.FillEllipse($nodeBrush, [float](10.0 * $scale), [float](6.5 * $scale), [float](3.1 * $scale), [float](3.1 * $scale))
            $Graphics.FillEllipse($nodeBrush, [float](18.8 * $scale), [float](23.5 * $scale), [float](3.1 * $scale), [float](3.1 * $scale))
            $Graphics.FillEllipse($nodeBrush, [float](22.3 * $scale), [float](9.4 * $scale), [float](3.3 * $scale), [float](3.3 * $scale))
            $curvePen.Dispose()
            $nodeBrush.Dispose()
        }
        'autobbox_recalc' {
            $pen = New-Object System.Drawing.Pen($fg, [float](2.2 * $scale))
            $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
            $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
            $Graphics.DrawArc($pen, [float](7.0 * $scale), [float](8.0 * $scale), [float](13.0 * $scale), [float](13.0 * $scale), 210, 215)
            $Graphics.DrawArc($pen, [float](12.0 * $scale), [float](11.0 * $scale), [float](13.0 * $scale), [float](13.0 * $scale), 30, 215)
            $arrowBrush = New-Object System.Drawing.SolidBrush($fg)
            Fill-Polygon -Graphics $Graphics -Brush $arrowBrush -Scale $scale -Pairs @(
                @(8.3, 10.5), @(6.0, 10.0), @(7.3, 12.0)
            )
            Fill-Polygon -Graphics $Graphics -Brush $arrowBrush -Scale $scale -Pairs @(
                @(23.7, 21.5), @(26.0, 22.0), @(24.7, 20.0)
            )
            $arrowBrush.Dispose()
            $pen.Dispose()
        }
        'autobbox_top2' {
            $fillBrush = New-Object System.Drawing.SolidBrush($fg)
            $softBrush = New-Object System.Drawing.SolidBrush((New-Color 214 224 235 180))
            $Graphics.FillRectangle($fillBrush, [float](8.0 * $scale), [float](9.0 * $scale), [float](16.0 * $scale), [float](4.6 * $scale))
            $Graphics.FillRectangle($fillBrush, [float](8.0 * $scale), [float](15.2 * $scale), [float](16.0 * $scale), [float](4.6 * $scale))
            $Graphics.FillRectangle($softBrush, [float](10.5 * $scale), [float](21.2 * $scale), [float](11.0 * $scale), [float](3.4 * $scale))
            $digitBrush = New-Object System.Drawing.SolidBrush($fgSoft)
            $font = New-Object System.Drawing.Font('Segoe UI', [float](9.5 * $scale), [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
            $Graphics.DrawString('2', $font, $digitBrush, [float](18.8 * $scale), [float](5.8 * $scale))
            $font.Dispose()
            $digitBrush.Dispose()
            $softBrush.Dispose()
            $fillBrush.Dispose()
        }
    }
}

function Save-Icon {
    param(
        [string]$Name,
        [int]$Size,
        [System.Drawing.Color]$FillTop,
        [System.Drawing.Color]$FillBottom,
        [string[]]$Targets
    )

    $bitmap = New-Object System.Drawing.Bitmap($Size, $Size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    Draw-Tile -Graphics $graphics -Size $Size -FillTop $FillTop -FillBottom $FillBottom | Out-Null
    Draw-IconGlyph -Graphics $graphics -Name $Name -Size $Size

    foreach ($target in $Targets) {
        $dir = Split-Path -Path $target -Parent
        if (!(Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        $bitmap.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Output "Wrote $target"
    }

    $graphics.Dispose()
    $bitmap.Dispose()
}

function Get-IconTargets {
    param(
        [string]$Root,
        [string]$Name,
        [int]$Size
    )

    $suffix = if ($Size -eq 32) { '_large' } else { '' }
    $file = "$Name$suffix.png"
    return @(
        (Join-Path $Root "text\resource\$file")
    )
}

function New-PreviewBoard {
    param(
        [string]$Root,
        [object[]]$Specs
    )

    $cellWidth = 96
    $cellHeight = 84
    $cols = 4
    $rows = [int][math]::Ceiling($Specs.Count / $cols)
    $width = ($cols * $cellWidth) + 24
    $height = ($rows * $cellHeight) + 24

    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.Clear((New-Color 245 247 250))

    $titleFont = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $textBrush = New-Object System.Drawing.SolidBrush((New-Color 49 61 74))
    $framePen = New-Object System.Drawing.Pen((New-Color 220 226 234), 1)

    for ($i = 0; $i -lt $Specs.Count; $i++) {
        $spec = $Specs[$i]
        $col = $i % $cols
        $row = [int][math]::Floor($i / $cols)
        $x = 12 + ($col * $cellWidth)
        $y = 12 + ($row * $cellHeight)
        $graphics.DrawRectangle($framePen, $x, $y, 80, 68)

        $iconPath = Join-Path $Root "text\resource\$($spec.Name)_large.png"
        $icon = [System.Drawing.Image]::FromFile($iconPath)
        $graphics.DrawImage($icon, $x + 24, $y + 6, 32, 32)
        $graphics.DrawString($spec.Label, $titleFont, $textBrush, [float]($x + 6), [float]($y + 46))
        $icon.Dispose()
    }

    $out = Join-Path $Root 'docs\plugin_icons_preview.png'
    $bitmap.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output "Wrote $out"

    $framePen.Dispose()
    $textBrush.Dispose()
    $titleFont.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

$root = 'F:\\claude\\003'
$iconSpecs = @(
    @{ Name = 'autobbox_size'; Label = 'Size'; Top = (New-Color 55 129 230); Bottom = (New-Color 33 93 183) }
    @{ Name = 'autobbox_volume'; Label = 'Volume'; Top = (New-Color 74 117 223); Bottom = (New-Color 56 76 178) }
    @{ Name = 'autobbox_iso'; Label = 'Iso'; Top = (New-Color 23 156 179); Bottom = (New-Color 12 121 145) }
    @{ Name = 'autobbox_dwg3'; Label = 'Views'; Top = (New-Color 232 144 39); Bottom = (New-Color 190 102 18) }
    @{ Name = 'autobbox_scale_sync'; Label = 'Scale'; Top = (New-Color 66 140 94); Bottom = (New-Color 38 102 62) }
    @{ Name = 'autobbox_param_tool'; Label = 'BOM'; Top = (New-Color 33 119 210); Bottom = (New-Color 0 157 154) }
    @{ Name = 'autobbox_delete'; Label = 'Delete'; Top = (New-Color 214 86 86); Bottom = (New-Color 174 54 54) }
    @{ Name = 'autobbox_split'; Label = 'Split'; Top = (New-Color 163 111 34); Bottom = (New-Color 126 79 14) }
    @{ Name = 'autobbox_rel_clean'; Label = 'Rel Clean'; Top = (New-Color 88 112 140); Bottom = (New-Color 62 79 102) }
    @{ Name = 'autobbox_rel_add'; Label = 'Rel Add'; Top = (New-Color 29 133 120); Bottom = (New-Color 19 97 87) }
    @{ Name = 'autobbox_parts'; Label = 'Part'; Top = (New-Color 69 131 220); Bottom = (New-Color 47 92 182) }
    @{ Name = 'autobbox_asm'; Label = 'Asm'; Top = (New-Color 40 122 155); Bottom = (New-Color 25 87 116) }
    @{ Name = 'autobbox_surface'; Label = 'Surface'; Top = (New-Color 37 164 118); Bottom = (New-Color 21 123 86) }
    @{ Name = 'autobbox_curve'; Label = 'Curve'; Top = (New-Color 32 154 198); Bottom = (New-Color 19 113 159) }
    @{ Name = 'autobbox_recalc'; Label = 'Recalc'; Top = (New-Color 217 129 44); Bottom = (New-Color 173 95 18) }
    @{ Name = 'autobbox_top2'; Label = 'Top2'; Top = (New-Color 86 103 123); Bottom = (New-Color 60 72 88) }
)

foreach ($spec in $iconSpecs) {
    foreach ($size in @(16, 32)) {
        Save-Icon `
            -Name $spec.Name `
            -Size $size `
            -FillTop $spec.Top `
            -FillBottom $spec.Bottom `
            -Targets (Get-IconTargets -Root $root -Name $spec.Name -Size $size)
    }
}

New-PreviewBoard -Root $root -Specs $iconSpecs
