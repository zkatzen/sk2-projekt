/********************************************************************************
** Form generated from reading UI file 'Clientwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.7.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CLIENTWINDOW_H
#define UI_CLIENTWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ClientWindow
{
public:
    QWidget *centralWidget;
    QLineEdit *destAddr;
    QSpinBox *portNumber;
    QPushButton *makeConnection;
    QTextEdit *messageBox;
    QLineEdit *messageInput;
    QPushButton *fileSend;
    QLineEdit *fileNameInput;
    QPushButton *stopButton;
    QMenuBar *menuBar;
    QToolBar *mainToolBar;
    QStatusBar *statusBar;

    void setupUi(QMainWindow *ClientWindow)
    {
        if (ClientWindow->objectName().isEmpty())
            ClientWindow->setObjectName(QStringLiteral("ClientWindow"));
        ClientWindow->resize(334, 404);
        centralWidget = new QWidget(ClientWindow);
        centralWidget->setObjectName(QStringLiteral("centralWidget"));
        destAddr = new QLineEdit(centralWidget);
        destAddr->setObjectName(QStringLiteral("destAddr"));
        destAddr->setGeometry(QRect(10, 10, 191, 31));
        portNumber = new QSpinBox(centralWidget);
        portNumber->setObjectName(QStringLiteral("portNumber"));
        portNumber->setGeometry(QRect(210, 10, 111, 31));
        portNumber->setMaximum(99999);
        portNumber->setValue(12345);
        makeConnection = new QPushButton(centralWidget);
        makeConnection->setObjectName(QStringLiteral("makeConnection"));
        makeConnection->setGeometry(QRect(70, 50, 181, 41));
        messageBox = new QTextEdit(centralWidget);
        messageBox->setObjectName(QStringLiteral("messageBox"));
        messageBox->setGeometry(QRect(10, 100, 311, 121));
        messageInput = new QLineEdit(centralWidget);
        messageInput->setObjectName(QStringLiteral("messageInput"));
        messageInput->setGeometry(QRect(10, 230, 311, 31));
        fileSend = new QPushButton(centralWidget);
        fileSend->setObjectName(QStringLiteral("fileSend"));
        fileSend->setGeometry(QRect(220, 270, 101, 41));
        fileNameInput = new QLineEdit(centralWidget);
        fileNameInput->setObjectName(QStringLiteral("fileNameInput"));
        fileNameInput->setGeometry(QRect(10, 280, 201, 22));
        stopButton = new QPushButton(centralWidget);
        stopButton->setObjectName(QStringLiteral("stopButton"));
        stopButton->setGeometry(QRect(219, 320, 101, 22));
        ClientWindow->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(ClientWindow);
        menuBar->setObjectName(QStringLiteral("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 334, 19));
        ClientWindow->setMenuBar(menuBar);
        mainToolBar = new QToolBar(ClientWindow);
        mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
        ClientWindow->addToolBar(Qt::TopToolBarArea, mainToolBar);
        statusBar = new QStatusBar(ClientWindow);
        statusBar->setObjectName(QStringLiteral("statusBar"));
        ClientWindow->setStatusBar(statusBar);

        retranslateUi(ClientWindow);

        QMetaObject::connectSlotsByName(ClientWindow);
    } // setupUi

    void retranslateUi(QMainWindow *ClientWindow)
    {
        ClientWindow->setWindowTitle(QApplication::translate("ClientWindow", "ClientWindow", 0));
        destAddr->setText(QApplication::translate("ClientWindow", "localhost", 0));
        makeConnection->setText(QApplication::translate("ClientWindow", "Connect!", 0));
        fileSend->setText(QApplication::translate("ClientWindow", "Send!", 0));
        fileNameInput->setText(QApplication::translate("ClientWindow", "... .wav filename ...", 0));
        stopButton->setText(QApplication::translate("ClientWindow", "...stahp...", 0));
    } // retranslateUi

};

namespace Ui {
    class ClientWindow: public Ui_ClientWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CLIENTWINDOW_H
