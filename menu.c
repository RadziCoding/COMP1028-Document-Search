#include <stdio.h>

void print_menu();
void returnToMenu();


void print_Menu()
  {
    int choice;

    do
    {
        printf("\n===== MAIN MENU =====\n");
        printf("1. Search\n");
        printf("2. View document\n");
        printf("3. Load collection\n");
        printf("4. Build index\n");
        printf("5. Show statistics\n");
        printf("6. Save results/report\n");
        printf("7. Settings\n");
        printf("8. Start benchmark test")
        printf("9. Exit\n");
        printf("=====================\n");

        printf("Enter your choice: ");
        scanf("%d", &choice);

        switch(choice)
        {
            case 1:
                do_search();
                break;

            case 2:
                view_document(); //To be built by Yaish
                break;

            case 3:
                load_collection();
                break;

            case 4:
                build_index();
                break;

            case 5:
                show_statistics();
                break;

            case 6:
                save_report();
                break;

            case 7:
                settings(); //To be built by Yaish
                break;

            case 8:
                benchmark(); //To be built by Yaish
                break;

            case 9:
                printf("Exiting program");
            
            default:
                printf("\nInvalid choice. Please select the available options from 1-8.\n");
        }

    } while(choice != 9);
}

void returnToMenu()
{
    printf("\nPress ENTER to return to the main menu...");

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF);

    getchar();
    
    printf("Returning to main menu...\n");
    
}
