#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>

class RenameFileDialog : public QDialog {
	Q_OBJECT

public:
	RenameFileDialog(QWidget *parent, std::string title);

	// Returns true if user clicks OK, false otherwise
	// userTextInput returns string that user typed into dialog
	static bool AskForName(QWidget *parent, std::string title, std::string &name);

private:
	QLineEdit *userText;
};

class FilenameFormatDialog : public QDialog {
	Q_OBJECT
public:
	FilenameFormatDialog(QWidget *parent);
	QLineEdit *userText;
};
