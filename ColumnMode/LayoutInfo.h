#pragma once

class LayoutInfo
{
public:

	D2D1_RECT_F GetLayoutRectangleInScreenSpace() const
	{
		return m_layoutRectangleInScreenSpace;
	}

	D2D1_RECT_F GetLayoutRectangleInScreenSpaceLockedToPixelCenters() const
	{
		return m_layoutRectangleInScreenSpaceLockedToPixelCenters;
	}

	int GetLayoutWidth() const
	{
		return static_cast<int>(m_layoutMetrics.width);
	}

	int GetLayoutHeight() const
	{
		return static_cast<int>(m_layoutMetrics.height);
	}

	D2D1_POINT_2F GetPosition() const
	{
		return m_layoutPosition;
	}

	int GetVisibleLineTop()
	{
		return (int)roundf(-(m_layoutRectangleInScreenSpaceLockedToPixelCenters.top / m_lineHeight));
	}

	FLOAT GetLineHeight()
	{
		return m_lineHeight;
	}

	// Setters

	void RefreshLayoutMetrics(IDWriteTextLayout* textLayout)
	{
		VerifyHR(textLayout->GetMetrics(&m_layoutMetrics));
		RefreshLayoutRectangleInScreenSpace();
		m_lineHeight = m_layoutMetrics.height / m_layoutMetrics.lineCount;
	}

	void SetPosition(D2D1_POINT_2F const& pt)
	{
		m_layoutPosition = pt;
		RefreshLayoutRectangleInScreenSpace();
	}

	void AdjustPositionX(float amount)
	{
		m_layoutPosition.x += amount;
		RefreshLayoutRectangleInScreenSpace();
	}

	void AdjustPositionY(float amount)
	{
		m_layoutPosition.y += amount;
		RefreshLayoutRectangleInScreenSpace();
	}

	void SetPositionX(float amount)
	{
		m_layoutPosition.x = amount;
		RefreshLayoutRectangleInScreenSpace();
	}

	void SetPositionY(float amount)
	{
		m_layoutPosition.y = amount;
		RefreshLayoutRectangleInScreenSpace();
	}

private:

	void RefreshLayoutRectangleInScreenSpace()
	{
		m_layoutRectangleInScreenSpace = D2D1::RectF(
			m_layoutPosition.x + m_layoutMetrics.left,
			m_layoutPosition.y + m_layoutMetrics.top,
			m_layoutPosition.x + m_layoutMetrics.left + m_layoutMetrics.widthIncludingTrailingWhitespace,
			m_layoutPosition.y + m_layoutMetrics.top + m_layoutMetrics.height);

		m_layoutRectangleInScreenSpaceLockedToPixelCenters = D2D1::RectF(
			floor(m_layoutRectangleInScreenSpace.left) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.top) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.right) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.bottom) + 0.5f);
	}

	DWRITE_TEXT_METRICS m_layoutMetrics;
	D2D1_POINT_2F m_layoutPosition;
	D2D1_RECT_F m_layoutRectangleInScreenSpace;
	D2D1_RECT_F m_layoutRectangleInScreenSpaceLockedToPixelCenters;
	FLOAT m_lineHeight;
};