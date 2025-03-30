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
    if (!m_window->controller()) {
		m_window->bootBIOS();
		if (!m_window->controller() || m_window->controller()->platform() != mPLATFORM_GBA) {
            QMessageBox* fail = new QMessageBox(QMessageBox::Warning, tr("Couldn't Connect"),
		                                    tr("Failed to boot BIOS."),
		                                    QMessageBox::Ok);
            fail->setAttribute(Qt::WA_DeleteOnClose);
            fail->show();
			return;
		}
	}

    m_controller = m_window->controller();

    CoreController::Interrupter interrupter(m_controller);
    m_controller->attachTcpSocket();
	connect(m_controller.get(), &CoreController::stopping, this, &TcpConnector::detach);
	interrupter.resume();
}

void TcpConnector::detach()
{

}

void TcpConnector::updateAttached()
{
    bool isAttached = m_window->controller() && m_window->controller()->isTcpSocketConnected();

    m_ui.connect->setDisabled(isAttached);
    m_ui.disconnect->setEnabled(isAttached);
}