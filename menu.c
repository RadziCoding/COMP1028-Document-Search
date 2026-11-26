#include <stdio.h>

void mainMenu();
void option1();
void option2();
void option3();
void returnToMenu();

int main()
{
    mainMenu();

    return 0;
}

  void mainMenu()
  {
    int choice;

    do
    {
        printf("\n===== MAIN MENU =====\n");
        printf("1. Search documents\n");
        printf("2. View inverted index\n");
        printf("3. Generate statistics report\n");
        printf("4. Exit\n");
        printf("=====================\n");

        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch(choice)
        {
            case 1:
                option1();
                break;

            case 2:
                option2();
                break;

            case 3:
                option3();
                break;

            case 4:
                printf("\nExiting program...\n");
                break;

            default:
                printf("\nInvalid choice. Try again.\n");
        }

    } while(choice != 4);
}

void option1()
{
    printf("Search for Documents\n");
    printf("Enter your query:\n");
    returnToMenu();
}

void option2()
{
    printf("View Inverted Index:\n");
    printf("Loading.....\n");
    returnToMenu();
}

void option3()
{
    printf("Generating Statistics Report...\n");
    printf("Complete!\n");
    returnToMenu();
}

void returnToMenu()
{
    printf("\nPress ENTER to return to the main menu...");

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF);

    getchar();
    
    printf("Returning to main menu...\n");
    
}
