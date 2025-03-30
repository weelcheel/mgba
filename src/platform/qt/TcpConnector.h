#pragma once

#include "ui_TcpConnector.h"

#include <memory>

namespace QGBA {
    class CoreController;
    class Window;
    
    class TcpConnector : public QDialog {
        Q_OBJECT
        
        public:
        TcpConnector(Window* window, QWidget* parent = nullptr);
        
        public slots:
            void attach();
            void detach();
        
        private slots:
            void updateAttached();
        
        private:
            Ui::TcpConnector m_ui;
        
            std::shared_ptr<CoreController> m_controller;
            Window* m_window;
    };   
}
    