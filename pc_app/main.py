"""Entry point for the ASF EFI PC tuning application."""

from gui.main_window import MainWindow


def main() -> None:
    app = MainWindow()
    app.mainloop()


if __name__ == "__main__":
    main()
