package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/signal"
	"syscall"

	_ "github.com/go-sql-driver/mysql"
	"github.com/livekit/client-unity-demo/demo-api/pkg/config"
	"github.com/livekit/client-unity-demo/demo-api/pkg/logger"
	"github.com/livekit/client-unity-demo/demo-api/pkg/service"
	"github.com/urfave/cli"
)

func main() {
	app := &cli.App{
		Name: "unity-demo-api",
		Flags: []cli.Flag{
			&cli.StringFlag{
				Name:  "config",
				Usage: "path to the config file",
			},
		},
		Action: startServer,
	}

	if err := app.Run(os.Args); err != nil {
		fmt.Println(err)
	}
}

func startServer(c *cli.Context) error {
	configFile := c.String("config")

	var configBody string
	if configFile == "" {
		configBody = os.Getenv("LK_UNITY_CONFIG")
	} else {
		content, err := ioutil.ReadFile(configFile)
		if err != nil {
			return err
		}

		configBody = string(content)
	}

	conf, err := config.NewConfig(configBody)
	if err != nil {
		return err
	}

	if conf.Development {
		logger.InitDevelopment(conf.LogLevel)
	} else {
		logger.InitProduction(conf.LogLevel)
	}

	server := service.NewUnityAPI(conf)
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT)

	go func() {
		sig := <-sigChan
		logger.Infow("exit requested, shutting down", "signal", sig)
		server.Stop()
	}()

	return server.Start()
}
