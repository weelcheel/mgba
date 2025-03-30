#include "TcpConnector.h"

#include <QMessageBox>

#include "CoreController.h"
#include "Window.h"
#include "utils.h"

using namespace QGBA;

TcpConnector::TcpConnector(Window* window, QWidget* parent)
	: QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint)
	, m_window(window)
{
	m_ui.setupUi(this);

	connect(window, &QObject::destroyed, this, &QWidget::close);
	connect(m_ui.connect, &QAbstractButton::clicked, this, &TcpConnector::attach);
	connect(m_ui.disconnect, &QAbstractButton::clicked, this, &TcpConnector::detach);

	updateAttached();
}

void TcpConnector::attach()
{

}

void TcpConnector::detach()
{

}

void TcpConnector::updateAttached()
{

}